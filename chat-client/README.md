# 🐧 ChatClient - Tauri 桌面聊天室客户端

> 基于 **Tauri 1.7 + 原生 HTML/CSS/JS + Rust std::net** 开发的桌面聊天室客户端，
> 对接基于 muduo C++ Reactor 模型开发的裸 TCP 聊天室服务端。

---

## 一、整体架构

### 1.1 数据流分层（严格对齐需求）

```
┌──────────────────────────────────────────────────────────────────┐
│  前端 UI 层 (chat-client/src/)                                    │
│   index.html — DOM 结构，登录页/主页面双页面切换                 │
│   style.css  — 样式，紫绿渐变配色                                │
│   app.js     — 状态管理 + invoke Command + listen Tauri Event     │
└───────────┬──────────────────────────────────────────────────────┘
            │  invoke(name, args)   ／   listen(event, cb)
            │  (Tauri 内置 IPC)
┌───────────▼──────────────────────────────────────────────────────┐
│  Tauri Rust 层 (chat-client/src-tauri/src/)                       │
│   lib.rs       — Tauri Command 函数：register/login/...          │
│   tcp.rs       — TcpClient：封包/后台读线程/断线重连/app.emit()   │
│   proto.rs     — 导入 build.rs 生成的 chat.rs (protobuf types)   │
│   build.rs     — 执行时调用 prost-build 编译 chat.proto           │
└───────────┬──────────────────────────────────────────────────────┘
            │  std::net::TcpStream (原生裸 TCP，无 TLS)
            │  帧格式：[4B LE 长度][BaseMessage protobuf]
┌───────────▼──────────────────────────────────────────────────────┐
│  服务端 (Muduo C++)                                               │
│   muduo TcpServer — EPoll + ThreadPool 异步 DB                   │
│   Protobuf 协议 — BaseMessage 信封 + payload 具体业务              │
│   Redis Pub/Sub — 跨节点消息同步                                  │
│   MySQL 8.0 — 用户/好友/群组/离线消息持久化                        │
└──────────────────────────────────────────────────────────────────┘
```

### 1.2 技术选型说明

| 组件 | 选型 | 说明 |
|------|------|------|
| UI框架 | 原生 HTML/CSS/JS | 不引入 Vue/React，无构建步骤 |
| 桌面壳 | Tauri 1.7 | WebView 容器 + Rust 后端，10MB 级安装包 |
| 通信 | Rust `std::net::TcpStream` | 原生 TCP，无第三方网络库 |
| 序列化 | prost (Protobuf Rust 实现) | 与 C++ 服务端 chat.proto 完全一致 |
| 异步通知 | Tauri `AppHandle.emit()` | Rust → 前端 事件推送通道 |
| IPC 调用 | Tauri `invoke_handler!` | 前端 → Rust 命令调用 |

---

## 二、目录结构

```
chat-client/
├── src/                          # 前端 UI（原生 HTML/CSS/JS，无构建）
│   ├── index.html                # 单页面：登录页 + 主页面
│   ├── style.css                 # 样式（渐变配色 + 完整组件）
│   └── app.js                    # 状态管理 / invoke / 事件监听
│
└── src-tauri/                    # Tauri Rust 后端
    ├── Cargo.toml                # 依赖（tauri / prost / bytes / chrono…）
    ├── build.rs                  # 编译时 prost-build 处理 chat.proto
    ├── tauri.conf.json           # Tauri 配置（窗口大小 / allowlist / bundle）
    └── src/
        ├── main.rs               # 二进制入口：调用 chat_client_lib::run()
        ├── lib.rs                # 核心库：Tauri Command + run()
        ├── tcp.rs                # TCP 连接管理（最关键的一层）
        └── proto.rs              # 导入 build.rs 生成的 protobuf 代码
```

---

## 三、关键实现说明

### 3.1 通信帧格式（与 muduo 服务端一字节不差）

```
+------------------+-----------------------------------+
|  4 Bytes         |         N Bytes                   |
|  (Little Endian) |   BaseMessage.encode()           |
|  payload_length  |   (含 type + payload 字段)        |
+------------------+-----------------------------------+
```

**发送端**（`tcp.rs: TcpClient::send_msg()`）：
```rust
let base = BaseMessage { type: msg_type, payload };   // protobuf 信封
let body = base.encode_to_vec();                      // 编码
let mut frame = vec![];
frame.put_u32_le(body.len() as u32);                  // 4B 长度前缀
frame.extend_from_slice(&body);
stream.write_all(&frame);                              // 写入 socket
```

**接收端**（`tcp.rs: TcpClient::try_parse_frame()`）：
```rust
if buf.len() < 4 { return None; }                     // 不够长度
let payload_len = u32::from_le_bytes(buf[0..4]);
if buf.len() < 4 + payload_len { return None; }       // 不够一整帧
let msg = BaseMessage::decode(&buf[4..4+payload_len]);
return Some((msg, 4 + payload_len));                  // (消息, 消费字节数)
```

### 3.2 后台读线程（不阻塞主线程）

`login` Command 成功后启动独立后台线程：

```
reader_loop
    ↓
loop {
    match stream.read(&mut tmp) {
        Ok(0)  => 远端断开 → try_reconnect() → break
        Ok(n)  → buf.extend → while let Some(帧) = try_parse_frame()
                            { dispatch(帧) }
        Err(timeout) => continue // 1s 超时，只检查 running 标志
        Err(other)  => 网络异常 → try_reconnect() → break
    }
}
```

关键参数：
- `set_read_timeout(Some(Duration::from_secs(1)))`：避免永久阻塞在 `recv` 上，保证 running=false 时 1s 内退出
- `set_nodelay(true)`：关闭 Nagle，降低小包延迟
- `TcpStream::clone()`：跨线程共享同一个内核 fd

### 3.3 消息分发（dispatch → emit → 前端）

```
BaseMessage 解码
    │
    ├── type=2  LOGIN_MSG_ACK    → emit chat.login_ack       { ok, id, username, msg }
    ├── type=5  REG_MSG_ACK      → emit chat.reg_ack         { ok, code, msg, id }
    ├── type=6  ONE_CHAT_MSG     → emit chat.one_chat        { from_id, to_id, content, … }
    ├── type=7  ONE_CHAT_ACK     → emit chat.one_chat_ack    { ok, code, msg }
    ├── type=18 GROUP_CHAT_MSG   → emit chat.group_chat      { from_id, group_id, content, … }
    ├── type=19 GROUP_CHAT_ACK   → emit chat.group_chat_ack  { ok, code, msg }
    ├── type=9  ADD_FRIEND_ACK   → emit chat.add_friend_ack  { ok, code, msg }
    ├── type=11 DEL_FRIEND_ACK   → emit chat.del_friend_ack  { ok, code, msg }
    ├── type=3  LOGINOUT_MSG     → emit chat.loginout_ack    { ok }
    └── 其他                      → 日志忽略（跨节点消息等）
```

### 3.4 断线重连策略

```
reader_loop 检测到 disconnect (EOF / Err)
    │
    ├─ 有当前登录用户 (current_user.is_some())
    │   ├─ 通知前端 chat.net_status → { status: "reconnecting" }
    │   ├─ TcpStream::connect(addr) 重新连接
    │   ├─ 连接成功 → { status: "reconnected" }
    │   ├─ 连接失败 → 等待下次 reader 触发（> 3s 防抖）
    │   └─ 由于密码不保存不自动登录 → 向前端发送 chat.login_ack
    │       { ok:false, need_relogin:true, msg:"断线重连后需要重新登录" }
    │
    └─ 无登录用户 → 仅 emit chat.net_status:"disconnected"，不自动重连
```

### 3.5 错误码映射

与 C++ 服务端 chatservice 中定义的完全一致：

| code | 含义 |
|------|------|
| 0 | 成功 |
| 1 | 参数非法 |
| 2 | 用户名已存在 / 目标不存在 |
| 3 | 数据库写入失败 |
| 4 | 用户不存在 |
| 5 | 密码错误 |
| 6 | 已是好友 / 已是群成员 |
| 7 | 权限不足 |

Rust 端在 `tcp.rs: code_msg()` 中做描述映射，前端 `handleRegAck` 等直接显示。

---

## 四、构建与运行

### 4.1 环境准备

```bash
# 1. 安装 Rust（跳过如果已装）
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"

# 2. 安装 protoc（已安装则跳过）
sudo apt-get install -y protobuf-compiler

# 3. 安装 Tauri 系统依赖（Linux / Ubuntu 22.04）
sudo apt-get install -y \
    libwebkit2gtk-4.0-dev \
    build-essential \
    curl \
    wget \
    file \
    libssl-dev \
    libgtk-3-dev \
    libayatana-appindicator3-dev \
    librsvg2-dev

# 4. 安装 Tauri CLI
cargo install tauri-cli --version "^1.7"
```

### 4.2 启动服务端（先启动才能连）

```bash
# 初始化数据库
python3 /home/wangt/ThreadPoolAction/sql/init_db.py
# 启动 Redis
sudo systemctl start redis-server
# 启动 nginx 负载均衡
sudo systemctl start nginx
# 启动 2 个 chat 节点（或 1 个节点也可）
./bin/log_threadpool_test 127.0.0.1 6000 &
./bin/log_threadpool_test 127.0.0.1 6001 &
```

### 4.3 开发模式运行 Tauri

```bash
cd /home/wangt/ThreadPoolAction/chat-client/src-tauri
cargo tauri dev    # 编译并打开桌面窗口
```

### 4.4 打包生产构建

```bash
cd /home/wangt/ThreadPoolAction/chat-client/src-tauri
cargo tauri build  # 产物在 src-tauri/target/release/bundle/
```

输出产物：
- **Linux**：`.deb` / `.AppImage`
- **Windows**：`.msi` / `.exe`
- **macOS**：`.dmg` / `.app`

### 4.5 服务器地址配置

打开客户端 → 切换到「服务器」Tab → 修改地址 → 点击连接。

默认值：
- `127.0.0.1:8080`（走 Nginx TCP 负载均衡）
- 单节点调试可改为 `127.0.0.1:6000` 或 `127.0.0.1:6001`

---

## 五、功能清单

### ✅ 已实现

| 模块 | 功能 | 说明 |
|------|------|------|
| 认证 | 用户注册 | 参数校验 → REG_MSG → REG_MSG_ACK → 显示新用户 ID |
| 认证 | 用户登录 | LOGIN_MSG → LOGIN_MSG_ACK → 记录当前用户 → 启动读线程 |
| 认证 | 注销登录 | LOGINOUT_MSG → 断开连接 → 回到登录页 |
| 社交 | 好友列表 | 左侧栏展示，在线/离线状态点，点击切换会话 |
| 社交 | 添加好友 | 弹窗输入用户 ID → ADD_FRIEND_MSG → ACK 后本地更新 |
| 社交 | 删除好友 | 好友项 hover 显示删除按钮 → DEL_FRIEND_MSG |
| 聊天 | 一对一私聊 | 选择好友 → 输入消息 → ONE_CHAT_MSG → 回执确认 |
| 聊天 | 私聊消息推送 | 好友发送的消息通过 reader_loop 捕获，实时显示气泡 |
| 聊天 | 群聊发送 | 选择群组 → GROUP_CHAT_MSG |
| 聊天 | 群聊消息推送 | 群成员消息推送，显示发言用户名 |
| 稳定性 | 后台读线程 | 独立 std::thread，1s 超时检查，不阻塞 UI |
| 稳定性 | 断线重连 | EOF / 网络异常 → 防抖 3s → 重连 → 提示重新登录 |
| 稳定性 | 网络状态 | 登录页/主页面 双位置显示连接指示灯 |
| 缓存 | 本地消息 | localStorage 按会话缓存，下次打开可恢复 |
| 体验 | 错误提示 | Toast 浮层，4 种颜色等级（success/error/warn/info） |

### 📌 服务端需配合接口（已预留）

| 功能 | 服务端消息类型 | 状态 |
|------|---------------|------|
| 好友列表查询 | `QUERY_FRIENDS_MSG` (待新增) | 客户端 get_friends 已预留空实现 |
| 创建群组 | `CREATE_GROUP_MSG=12` | 服务端已有 handler，前端弹窗已实现 |
| 加入群组 | `ADD_GROUP_MSG=14` | 服务端已有 handler，前端弹窗已实现 |
| 群成员列表查询 | 待新增消息类型 | 前端右侧详情面板预留展示位 |

---

## 六、异常处理

| 异常类型 | 处理方式 | 用户感知 |
|----------|----------|----------|
| TCP 连接失败 | `connect()` 返回 Err → 前端 toast 红色错误 | 连接按钮下方显示错误信息 |
| TCP 中途断开 | reader_loop recv EOF → chat.net_status:disconnected → toast | 断线提示 + 自动重连指示灯闪烁 |
| 服务端重连后未登录 | 自动重连 socket，然后 emit login_ack(need_relogin) | 顶部 toast "连接已恢复，请重新登录"，留在主页面 |
| 服务端返回业务错误（code>0） | 各个 Ack handler 显示错误文案 + toast | 表单下方/右下角浮层 |
| 登录失效（未登录执行操作） | get_me() 返回 None → Rust Command 返回 Err("尚未登录") | toast "尚未登录" 并回到登录页 |
| invoke 网络失败 | Promise.catch 捕获 → toast 红色 | 即时提示 |
| 好友不存在 / 群不存在 | ADD_FRIEND_MSG / ADD_GROUP_MSG 返回 code=2 | toast "目标不存在" |

---

## 七、与 muduo 服务端协议完全对齐说明

本客户端**完全不修改服务端协议**，全部实现基于已有 `chat.proto`：

- 消息枚举类型值 (`LOGIN_MSG=1` … `GROUP_CHAT_ACK=19`) 严格一致
- 帧格式 4B LE + protobuf：muduo 源码中 `muduo::net::TcpConnection` 通过 `Buffer::prependable` 处理长度前缀，客户端实现同样逻辑
- BaseMessage envelope 结构：`EnMsgType type + bytes payload` 与 C++ 版本 byte-for-byte 兼容
- 所有 Request / Ack 字段名与编号一致（通过 `prost-build` 直接编译同一个 `.proto`）

---

## 八、调试与日志

Rust 日志（env_logger）：
```bash
# 调试级别日志
RUST_LOG=debug cargo tauri dev

# 仅看 TCP 模块
RUST_LOG=chat_client_lib::tcp=debug cargo tauri dev
```

前端日志：浏览器 Developer Tools → Console（Tauri 的 WebView 内置），app.js 内有 `console.warn` 记录 mock 路径。

---

## 九、扩展方向

1. **AI 智能客服**：前端新增 "AI 客服" Tab，调用 `chat_ai` FastAPI 服务（浏览器端 fetch 即可，无需走 Rust）
2. **图片/文件传输**：`chat.proto` 新增 `FILE_MSG` 类型，Rust 端 `tokio-tungstenite` 或独立 HTTP 上传通道
3. **视频语音**：WebView 中启用 WebRTC，Rust 端提供 ICE 信令
4. **多语言 i18n**：app.js 中抽离字符串常量为 JSON 字典

---

## 十、常见问题

**Q1: 编译报错 "can't find library chat_client_lib"？**
答：确保 Cargo.toml 中 `[lib].path = "src/lib.rs"` 且 `src/lib.rs` 文件存在。

**Q2: Protobuf 字段不匹配？**
答：不要手动改 Rust 代码，用 `build.rs` 让 prost-build 直接编译 `../../chatsystem/chat.proto`，改 proto 后重新 `cargo check` 即可。

**Q3: 服务端收不到消息？**
答：先用 `tests/Client.cpp` 验证服务端可用，再看 Tauri 日志 `RUST_LOG=debug` 中的 `[reader]` / `[dispatch]` 跟踪。检查端口是否一致（默认 nginx:8080）。

**Q4: 消息粘包？**
答：4B 长度前缀设计天然处理粘包，reader_loop 里 `try_parse_frame` 循环解析，一次 recv 可能解析多帧。

**Q5: 断网后不自动重连？**
答：reader_loop 设置了 1s 读超时 + 3s 防抖。只有**登录过的用户**才触发重连（不登录重连没意义）。
