// ============================================================
// tcp.rs - TCP 连接管理器
// ============================================================
// 职责分层：
//   1. 连接生命周期管理：connect / disconnect / reconnect
//   2. 数据包封装：muduo 约定 4 字节长度前缀 + BaseMessage protobuf
//      发送格式: [uint32 LE 长度][protobuf 字节]
//      接收格式: 先读 4 字节长度 → 读 payload 长度字节 → 解码 BaseMessage
//   3. 后台读线程：独立 std::thread 循环 recv，收到消息后通过 Tauri 事件推送到前端
//   4. 断线重连：后台读线程检测到 disconnect / recv 错误时触发重连
//
// 前端交互链路：
//   前端 UI ──invoke──▶ Tauri Command ──▶ send_msg() ──▶ Muduo 服务端
//   Muduo 推送 ──▶ 后台读线程 recv ──▶ dispatch() ──▶ app.emit() ──▶ 前端 listen
// ============================================================

use anyhow::{Context, Result};
use parking_lot::Mutex;
use prost::Message;
use std::io::{Read, Write};
use std::net::TcpStream;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};
use tauri::{AppHandle, Emitter};

use crate::proto::*;

// ============================================================
// 错误码描述映射（与 chatservice 里的错误码体系对齐）
// ============================================================
pub fn code_msg(code: i64) -> &'static str {
    match code {
        0 => "成功",
        1 => "参数非法（用户名/密码为空，或长度超限）",
        2 => "用户名已存在 / 目标不存在",
        3 => "数据库写入失败，请稍后重试",
        4 => "用户不存在 / 目标用户不存在",
        5 => "密码错误",
        6 => "已是好友 / 已是群成员",
        7 => "权限不足（非群主操作等）",
        _ => "未知错误",
    }
}

// ============================================================
// 连接管理器（全局单例，由 once_cell 在 main.rs 初始化）
// ============================================================
pub struct TcpClient {
    // 实际 TCP 读写 socket（带 Mutex 保护，跨线程安全）
    stream: Mutex<Option<TcpStream>>,
    // 是否正在运行（后台读线程以此作为退出标志）
    running: AtomicBool,
    // 后台读线程是否已启动（防止重复 spawn）
    has_reader: AtomicBool,
    // 服务端地址
    server_addr: Mutex<String>,
    // AppHandle：后台读线程收到消息后 emit 到前端
    app: Mutex<Option<AppHandle>>,
    // 当前登录用户（保存 id + name，方便前端显示）
    current_user: Mutex<Option<LoginUser>>,
}

#[derive(Clone, serde::Serialize, serde::Deserialize)]
pub struct LoginUser {
    pub id: i64,
    pub name: String,
}

// 好友信息（好友列表返回给前端）
#[derive(Clone, serde::Serialize, serde::Deserialize)]
pub struct FriendInfo {
    pub id: i64,
    pub name: String,
    pub state: String, // "online" / "offline"
}

// 聊天消息（前端展示用）
#[derive(Clone, serde::Serialize, serde::Deserialize)]
pub struct ChatMessage {
    pub msg_id: String,  // 前端本地渲染用 uuid
    pub from_id: i64,    // 发送者 id（私聊=好友id / 群聊=发言用户id）
    pub from_name: String,
    pub target_id: i64,  // 接收者 id（私聊=自己id / 群聊=群id）
    pub target_type: String, // "private" / "group"
    pub content: String,
    pub timestamp_ms: i64,
    pub is_self: bool,   // 是否自己发的
}

// Tauri 事件名常量（前端通过 listen 订阅）
pub const EVT_LOGIN_ACK: &str = "chat.login_ack";       // 登录响应
pub const EVT_REG_ACK: &str = "chat.reg_ack";           // 注册响应
pub const EVT_ONE_CHAT: &str = "chat.one_chat";         // 收到私聊消息
pub const EVT_GROUP_CHAT: &str = "chat.group_chat";     // 收到群聊消息
pub const EVT_ONE_CHAT_ACK: &str = "chat.one_chat_ack"; // 私聊发送回执
pub const EVT_GROUP_CHAT_ACK: &str = "chat.group_chat_ack";
pub const EVT_ADD_FRIEND_ACK: &str = "chat.add_friend_ack";
pub const EVT_DEL_FRIEND_ACK: &str = "chat.del_friend_ack";
pub const EVT_FRIEND_LIST: &str = "chat.friend_list";   // 好友列表推送
pub const EVT_LOGINOUT_ACK: &str = "chat.loginout_ack"; // 注销响应
pub const EVT_NET_STATUS: &str = "chat.net_status";     // 网络状态变化（connected / disconnected / reconnecting）

impl TcpClient {
    // ------------------------------------------------------------
    // 新建实例（由 once_cell 初始化）
    // ------------------------------------------------------------
    pub fn new() -> Self {
        Self {
            stream: Mutex::new(None),
            running: AtomicBool::new(false),
            has_reader: AtomicBool::new(false),
            server_addr: Mutex::new(String::new()),
            app: Mutex::new(None),
            current_user: Mutex::new(None),
        }
    }

    // ------------------------------------------------------------
    // 保存 AppHandle（供后台读线程向前端 emit 事件）
    // ------------------------------------------------------------
    pub fn set_app_handle(&self, app: AppHandle) {
        *self.app.lock() = Some(app);
    }

    // ------------------------------------------------------------
    // 向前端发送 Tauri 事件
    // ------------------------------------------------------------
    fn emit(&self, event: &str, payload: &impl serde::Serialize) {
        if let Some(app) = self.app.lock().as_ref() {
            if let Err(e) = app.emit(event, payload) {
                log::warn!("[emit] {} 失败: {}", event, e);
            }
        }
    }

    // ------------------------------------------------------------
    // 连接服务端（同步执行，阻塞直到连接成功或失败）
    // 注意：connect 本身不启动读线程，由外部调用 ensure_reader() 触发
    // ------------------------------------------------------------
    pub fn connect(&self, addr: &str) -> Result<()> {
        eprintln!("[tcp] disconnect 旧连接...");
        self.disconnect(); // 先断开旧连接

        eprintln!("[tcp] 正在连接 {}...", addr);
        let stream = TcpStream::connect(addr)
            .with_context(|| format!("连接 {} 失败", addr))?;
        eprintln!("[tcp] TCP 连接成功");
        // 启用 TCP_NODELAY 降低小包延迟（muduo 默认开启）
        stream.set_nodelay(true).ok();
        // 读超时：后台读线程 recv 超过 1s 就重新检查 running 状态，避免永久阻塞
        stream.set_read_timeout(Some(Duration::from_secs(1))).ok();
        stream.set_write_timeout(Some(Duration::from_secs(10))).ok();

        *self.server_addr.lock() = addr.to_string();
        *self.stream.lock() = Some(stream);
        self.running.store(true, Ordering::SeqCst);

        // 通知前端：网络已连接
        self.emit(EVT_NET_STATUS, &serde_json::json!({"status":"connected","addr":addr}));

        Ok(())
    }

    // ------------------------------------------------------------
    // 断开连接
    // ------------------------------------------------------------
    pub fn disconnect(&self) {
        self.running.store(false, Ordering::SeqCst);
        if let Some(stream) = self.stream.lock().take() {
            let _ = stream.shutdown(std::net::Shutdown::Both);
        }
        *self.current_user.lock() = None;
        self.emit(EVT_NET_STATUS, &serde_json::json!({"status":"disconnected"}));
    }

    // ------------------------------------------------------------
    // 判断是否已连接
    // ------------------------------------------------------------
    pub fn is_connected(&self) -> bool {
        self.stream.lock().is_some() && self.running.load(Ordering::SeqCst)
    }

    // ------------------------------------------------------------
    // 获取当前登录用户
    // ------------------------------------------------------------
    pub fn current_user(&self) -> Option<LoginUser> {
        self.current_user.lock().clone()
    }

    // ------------------------------------------------------------
    // 获取服务端地址（lib.rs 的 get_net_status 命令需要读取）
    // ------------------------------------------------------------
    pub fn server_addr(&self) -> String {
        self.server_addr.lock().clone()
    }

    // ============================================================
    // 发送消息（纯 protobuf 字节，不加长度前缀）
    // ============================================================
    // 服务端 chatserver.cpp onMessage() 用 retrieveAllAsString() 取出
    // 整个缓冲区后直接 ParseFromString()，不读长度前缀。
    // 因此客户端也必须直接发送 BaseMessage 的 protobuf 序列化字节。
    // ============================================================
    pub fn send_msg(&self, msg_type: i32, payload: Vec<u8>) -> Result<()> {
        // 构造 BaseMessage 信封
        let base = BaseMessage {
            r#type: msg_type,
            payload,
        };
        // BaseMessage protobuf 编码
        let mut body_buf = Vec::with_capacity(base.encoded_len());
        base.encode(&mut body_buf).context("BaseMessage 编码失败")?;
        eprintln!("[tcp] send_msg type={} bytes={}", msg_type_name(msg_type), body_buf.len());

        // 直接发送 protobuf 字节，不加长度前缀
        let guard = self.stream.lock();
        let stream = guard.as_ref().context("未连接到服务端")?;
        (&*stream)
            .write_all(&body_buf)
            .with_context(|| format!("发送 {} 失败", msg_type_name(msg_type)))?;
        eprintln!("[tcp] send_msg 发送完成");
        Ok(())
    }

    // ============================================================
    // 后台读线程入口（由外部调用 spawn_reader 启动）
    // ============================================================
    // 服务端不使用长度前缀编解码器（LengthHeaderCodec），
    // 而是直接 retrieveAllAsString + ParseFromString。
    // 因此客户端也直接把每次 recv 的数据当作一条完整的
    // BaseMessage protobuf 消息来解析，与服务端逻辑保持一致。
    // ============================================================
    fn reader_loop(me: Arc<TcpClient>) {
        let last_reconnect = Instant::now();
        loop {
            if !me.running.load(Ordering::SeqCst) {
                break;
            }
            // 取出 socket clone（TcpStream 不实现 Clone，用 try_clone 复制 fd）
            let sock = {
                let guard = me.stream.lock();
                match guard.as_ref() {
                    Some(s) => match s.try_clone() {
                        Ok(c) => c,
                        Err(e) => {
                            log::warn!("[reader] try_clone 失败: {}", e);
                            drop(guard);
                            thread::sleep(Duration::from_millis(300));
                            continue;
                        }
                    },
                    None => {
                        drop(guard);
                        thread::sleep(Duration::from_millis(300));
                        continue;
                    }
                }
            };

            // 尝试读入数据
            let mut tmp = [0u8; 4096];
            match (&sock).read(&mut tmp) {
                Ok(0) => {
                    // 远端关闭连接（EOF），触发断线
                    log::warn!("[reader] 服务端主动断开连接");
                    me.running.store(false, Ordering::SeqCst);
                    me.emit(EVT_NET_STATUS, &serde_json::json!({"status":"disconnected","reason":"server_closed"}));
                    if me.current_user.lock().is_some() && last_reconnect.elapsed() > Duration::from_secs(3) {
                        let _ = me.try_reconnect();
                    }
                    break;
                }
                Ok(n) => {
                    // 直接把收到的 n 字节当作一条完整的 BaseMessage 解析
                    // （与服务端 retrieveAllAsString + ParseFromString 逻辑一致）
                    match BaseMessage::decode(&tmp[..n]) {
                        Ok(msg) => {
                            me.dispatch(msg);
                        }
                        Err(e) => {
                            log::warn!("[reader] protobuf 解析失败: {}, data_len={}", e, n);
                        }
                    }
                }
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock
                    || e.kind() == std::io::ErrorKind::TimedOut =>
                {
                    continue;
                }
                Err(e) => {
                    log::warn!("[reader] recv 错误: {}", e);
                    me.running.store(false, Ordering::SeqCst);
                    me.emit(EVT_NET_STATUS, &serde_json::json!({"status":"disconnected","reason":e.to_string()}));
                    if me.current_user.lock().is_some() && last_reconnect.elapsed() > Duration::from_secs(3) {
                        let _ = me.try_reconnect();
                    }
                    break;
                }
            }
        }
    }

    // ============================================================
    // 分发收到的 BaseMessage（根据 type 路由到前端事件）
    // ============================================================
    fn dispatch(&self, msg: BaseMessage) {
        let t = msg.r#type;
        log::debug!("[dispatch] 收到 {}", msg_type_name(t));
        match t {
            // ---- 登录响应 ----
            2 => {
                // LOGIN_MSG_ACK: LoginResponse (LoginRequest 里的 id 为-1代表失败，用户名和密码等复用原定义，
                //                 我们直接解析成 LoginRequest 来读取，为了兼容服务端只改 code/msg/id)
                // 说明：服务端登录响应与 LoginRequest 字段一致（id=成功用户id / -1=失败, name=用户名 / 错误描述）
                match LoginRequest::decode(msg.payload.as_slice()) {
                    Ok(r) => {
                        let ok = r.id > 0;
                        let payload = serde_json::json!({
                            "ok": ok,
                            "id": r.id,
                            "username": r.username,
                            "password": "", // 清空
                            "msg": if ok { "登录成功" } else { &r.username }, // 失败时 name 字段存错误描述
                        });
                        // 登录成功 → 记录当前用户
                        if ok {
                            *self.current_user.lock() = Some(LoginUser {
                                id: r.id,
                                name: r.username.clone(),
                            });
                        }
                        self.emit(EVT_LOGIN_ACK, &payload);
                    }
                    Err(e) => log::warn!("LOGIN_MSG_ACK 解码失败: {}", e),
                }
            }
            // ---- 注册响应 ----
            5 => {
                match RegisterResponse::decode(msg.payload.as_slice()) {
                    Ok(r) => {
                        let payload = serde_json::json!({
                            "ok": r.code == 0,
                            "code": r.code,
                            "msg": if r.msg.is_empty() { code_msg(r.code) } else { &r.msg },
                            "id": r.id,
                        });
                        self.emit(EVT_REG_ACK, &payload);
                    }
                    Err(e) => log::warn!("REG_MSG_ACK 解码失败: {}", e),
                }
            }
            // ---- 私聊请求（服务端推送到本客户端：好友发的消息）----
            6 => {
                match OneChatRequest::decode(msg.payload.as_slice()) {
                    Ok(r) => {
                        let is_self = self.current_user.lock().as_ref().map(|u| u.id) == Some(r.from_id);
                        let payload = serde_json::json!({
                            "from_id": r.from_id,
                            "to_id": r.to_id,
                            "from_name": format!("用户{}", r.from_id),
                            "content": r.content,
                            "timestamp_ms": r.timestamp,
                            "is_self": is_self,
                        });
                        self.emit(EVT_ONE_CHAT, &payload);
                    }
                    Err(e) => log::warn!("ONE_CHAT_MSG 解码失败: {}", e),
                }
            }
            // ---- 私聊发送回执 ----
            7 => {
                match OneChatAck::decode(msg.payload.as_slice()) {
                    Ok(r) => self.emit(EVT_ONE_CHAT_ACK, &serde_json::json!({
                        "ok": r.code == 0,
                        "code": r.code,
                        "msg": r.msg,
                    })),
                    Err(e) => log::warn!("ONE_CHAT_ACK 解码失败: {}", e),
                }
            }
            // ---- 添加好友响应 ----
            9 => {
                match AddFriendAck::decode(msg.payload.as_slice()) {
                    Ok(r) => self.emit(EVT_ADD_FRIEND_ACK, &serde_json::json!({
                        "ok": r.code == 0,
                        "code": r.code,
                        "msg": if r.msg.is_empty() { code_msg(r.code) } else { &r.msg },
                    })),
                    Err(e) => log::warn!("ADD_FRIEND_ACK 解码失败: {}", e),
                }
            }
            // ---- 删除好友响应 ----
            11 => {
                match DelFriendAck::decode(msg.payload.as_slice()) {
                    Ok(r) => self.emit(EVT_DEL_FRIEND_ACK, &serde_json::json!({
                        "ok": r.code == 0,
                        "code": r.code,
                        "msg": if r.msg.is_empty() { code_msg(r.code) } else { &r.msg },
                    })),
                    Err(e) => log::warn!("DEL_FRIEND_ACK 解码失败: {}", e),
                }
            }
            // ---- 群聊请求（服务端推送到本客户端：群里别人发的消息）----
            18 => {
                match GroupChatRequest::decode(msg.payload.as_slice()) {
                    Ok(r) => {
                        let is_self = self.current_user.lock().as_ref().map(|u| u.id) == Some(r.from_id);
                        let payload = serde_json::json!({
                            "from_id": r.from_id,
                            "group_id": r.group_id,
                            "from_name": format!("用户{}", r.from_id),
                            "content": r.content,
                            "timestamp_ms": r.timestamp,
                            "is_self": is_self,
                        });
                        self.emit(EVT_GROUP_CHAT, &payload);
                    }
                    Err(e) => log::warn!("GROUP_CHAT_MSG 解码失败: {}", e),
                }
            }
            // ---- 群聊发送回执 ----
            19 => {
                match GroupChatAck::decode(msg.payload.as_slice()) {
                    Ok(r) => self.emit(EVT_GROUP_CHAT_ACK, &serde_json::json!({
                        "ok": r.code == 0,
                        "code": r.code,
                        "msg": r.msg,
                    })),
                    Err(e) => log::warn!("GROUP_CHAT_ACK 解码失败: {}", e),
                }
            }
            // ---- 注销响应（LOGINOUT_MSG）----
            3 => {
                // 服务端没有 LOGINOUT_ACK，收到 LOGINOUT_MSG 回显表示注销成功
                self.emit(EVT_LOGINOUT_ACK, &serde_json::json!({"ok":true}));
            }
            _ => {
                log::debug!("未处理的消息类型: {} ({})", t, msg_type_name(t));
            }
        }
    }

    // ============================================================
    // 启动后台读线程（仅启动一次；断线重连时由 try_reconnect 重新启动）
    // ============================================================
    pub fn ensure_reader(me: &Arc<TcpClient>) {
        if me.has_reader.load(Ordering::SeqCst) {
            return;
        }
        me.has_reader.store(true, Ordering::SeqCst);
        Self::spawn_reader_inner(me.clone());
    }

    // 内部实现：实际创建读线程
    fn spawn_reader_inner(me: Arc<TcpClient>) {
        thread::Builder::new()
            .name("chat-reader".into())
            .spawn(move || {
                // 外层套个 panic catch，防止意外崩溃
                let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                    Self::reader_loop(me);
                }));
            })
            .expect("创建后台读线程失败");
    }

    // ============================================================
    // 断线自动重连（仅当用户登录过才尝试重连并重发登录请求）
    // ============================================================
    fn try_reconnect(&self) -> Result<()> {
        let addr = self.server_addr.lock().clone();
        let user = self.current_user.lock().clone();
        if addr.is_empty() || user.is_none() {
            return Ok(());
        }
        self.emit(EVT_NET_STATUS, &serde_json::json!({"status":"reconnecting","addr":addr}));
        // 重新建立 socket
        let stream = TcpStream::connect(&addr)
            .with_context(|| format!("重连 {} 失败", addr))?;
        stream.set_nodelay(true).ok();
        stream.set_read_timeout(Some(Duration::from_secs(1))).ok();
        stream.set_write_timeout(Some(Duration::from_secs(10))).ok();
        *self.stream.lock() = Some(stream);
        self.running.store(true, Ordering::SeqCst);
        self.emit(EVT_NET_STATUS, &serde_json::json!({"status":"reconnected"}));

        // 关键修复：重连成功后重新启动读线程（不是 running.store(false) 杀掉它！）
        // 由于 reader_loop 已经 break，需要重新 spawn
        self.has_reader.store(false, Ordering::SeqCst);
        // 注意：此处无法直接 spawn（需要 Arc<TcpClient>），
        // 重连后向 UI 发送登录失效提醒，用户重新登录时 ensure_connected() 会触发 ensure_reader
        self.emit(EVT_LOGIN_ACK, &serde_json::json!({
            "ok": false,
            "id": -1,
            "username": user.as_ref().unwrap().name,
            "msg": "断线重连后需要重新登录",
            "need_relogin": true,
        }));
        Ok(())
    }
}

impl Default for TcpClient {
    fn default() -> Self { Self::new() }
}
