// ============================================================
// lib.rs - Tauri 应用核心库
// ============================================================
// 代码分层：
//   mod proto     —— Protobuf 编译生成代码导入
//   mod tcp       —— TCP 连接管理（封包/收包/后台读线程/断线重连）
//   self          —— Tauri 框架初始化 + IPC Command 实现 + 应用启动
//
// 数据流分层：
//   ┌────────────────────────────────────────────────────────┐
//   │  前端 UI (原生 HTML/CSS/JS, /chat-client/src)          │
//   │        │ invoke  /  listen (Tauri IPC)                 │
//   └────────┼───────────────────────────────────────────────┘
//            ▼
//   ┌────────────────────────────────────────────────────────┐
//   │  Tauri Command (本文件)                                │
//   │   register / login / send_private / send_group         │
//   │   add_friend / del_friend / get_friends / logout      │
//   │        │ send_msg() / spawn_reader()                   │
//   └────────┼───────────────────────────────────────────────┘
//            ▼
//   ┌────────────────────────────────────────────────────────┐
//   │  TcpClient (tcp.rs)                                    │
//   │  ┌────────────┐   4B长度+protobuf    ┌───────────────┐  │
//   │  │ send_msg() │────────────────────▶│ Muduo 服务端   │  │
//   │  └────────────┘                       └───────┬───────┘  │
//   │  ┌────────────┐                               │          │
//   │  │ reader     │◀──────────────────────────────┘          │
//   │  │ 后台线程   │  BaseMessage → dispatch()                │
//   │  └─────┬──────┘                                           │
//   └────────┼────────────────────────────────────────────────┘
//            ▼ app.emit()
//   ┌────────────────────────────────────────────────────────┐
//   │  前端 listen (登录响应 / 聊天消息 / 错误提示)           │
//   └────────────────────────────────────────────────────────┘
// ============================================================

pub mod proto;
pub mod tcp;

use anyhow::Result;
use once_cell::sync::Lazy;
use prost::Message;
use std::sync::Arc;

// 显式导入（避免 glob 导入与 #[tauri::command] 生成的宏符号冲突）
use crate::proto::{
    AddFriendRequest, DelFriendRequest, GroupChatRequest, LoginRequest, OneChatRequest,
    RegisterRequest,
};
use crate::tcp::{FriendInfo, LoginUser, TcpClient};

// ============================================================
// 全局单例：TcpClient（Arc 方便跨线程共享所有权）
// ============================================================
pub static TCP: Lazy<Arc<TcpClient>> = Lazy::new(|| Arc::new(TcpClient::new()));

// ============================================================
// Tauri run 入口（由 main.rs 的 main() 调用）
// ============================================================
#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let _ = env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info"))
        .try_init();

    tauri::Builder::default()
        // 注册所有 Tauri Command（前端通过 window.__TAURI__.invoke 调用）
        .invoke_handler(tauri::generate_handler![
            register,
            login,
            send_private,
            send_group,
            add_friend,
            del_friend,
            get_friends,
            logout,
            get_me,
            get_net_status,
            set_server,
        ])
        // 启动时把 AppHandle 保存进 TcpClient，供后台线程 emit 事件
        .setup(|app| {
            TCP.set_app_handle(app.handle().clone());
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("Tauri 应用启动失败");
}

// ============================================================
// 注册账号（前端 invoke → 本函数 → TCP.send_msg → 服务端）
// 参数: username, password
// 返回: ok 表示发送成功（真正结果由 EVT_REG_ACK 异步推送给前端）
// ============================================================
#[tauri::command]
fn register(username: String, password: String) -> Result<String, String> {
    let _ = env_logger::try_init();
    if username.trim().is_empty() || password.trim().is_empty() {
        return Err("用户名和密码不能为空".into());
    }
    if username.len() > 64 || password.len() > 64 {
        return Err("用户名或密码长度超限".into());
    }
    ensure_connected()?;

    let req = RegisterRequest {
        username,
        password,
        id: 0,
    };
    let mut payload = Vec::with_capacity(req.encoded_len());
    req.encode(&mut payload).map_err(|e| e.to_string())?;
    TCP.send_msg(4, payload).map_err(|e| e.to_string())?;
    Ok("已发送注册请求，请等待响应".into())
}

// ============================================================
// 用户登录
// ============================================================
#[tauri::command]
fn login(username: String, password: String) -> Result<String, String> {
    let _ = env_logger::try_init();
    eprintln!("[login] 收到前端调用 username={}", username);
    if username.trim().is_empty() || password.trim().is_empty() {
        return Err("用户名和密码不能为空".into());
    }
    eprintln!("[login] 调用 ensure_connected...");
    ensure_connected()?;
    eprintln!("[login] ensure_connected 成功，开始发送 LOGIN_MSG");

    let req = LoginRequest {
        username,
        password,
        id: 0,
    };
    let mut payload = Vec::with_capacity(req.encoded_len());
    req.encode(&mut payload).map_err(|e| e.to_string())?;
    TCP.send_msg(1, payload).map_err(|e| e.to_string())?;
    eprintln!("[login] LOGIN_MSG 已发送");

    Ok("已发送登录请求，请等待响应".into())
}

// ============================================================
// 发送私聊消息
// ============================================================
#[tauri::command]
fn send_private(to_id: i64, content: String) -> Result<String, String> {
    let user = TCP.current_user().ok_or("尚未登录")?;
    if to_id <= 0 || content.trim().is_empty() {
        return Err("参数非法".into());
    }
    ensure_connected()?;

    let req = OneChatRequest {
        from_id: user.id,
        to_id,
        content,
        timestamp: chrono::Utc::now().timestamp_millis(),
    };
    let mut payload = Vec::with_capacity(req.encoded_len());
    req.encode(&mut payload).map_err(|e| e.to_string())?;
    TCP.send_msg(6, payload).map_err(|e| e.to_string())?;
    Ok("已发送私聊".into())
}

// ============================================================
// 发送群聊消息
// ============================================================
#[tauri::command]
fn send_group(group_id: i64, content: String) -> Result<String, String> {
    let user = TCP.current_user().ok_or("尚未登录")?;
    if group_id <= 0 || content.trim().is_empty() {
        return Err("参数非法".into());
    }
    ensure_connected()?;

    let req = GroupChatRequest {
        from_id: user.id,
        group_id,
        content,
        timestamp: chrono::Utc::now().timestamp_millis(),
    };
    let mut payload = Vec::with_capacity(req.encoded_len());
    req.encode(&mut payload).map_err(|e| e.to_string())?;
    TCP.send_msg(18, payload).map_err(|e| e.to_string())?;
    Ok("已发送群聊".into())
}

// ============================================================
// 添加好友
// ============================================================
#[tauri::command]
fn add_friend(to_id: i64) -> Result<String, String> {
    let user = TCP.current_user().ok_or("尚未登录")?;
    if to_id <= 0 || to_id == user.id {
        return Err("不能添加自己为好友".into());
    }
    ensure_connected()?;

    let req = AddFriendRequest {
        from_id: user.id,
        to_id,
    };
    let mut payload = Vec::with_capacity(req.encoded_len());
    req.encode(&mut payload).map_err(|e| e.to_string())?;
    TCP.send_msg(8, payload).map_err(|e| e.to_string())?;
    Ok("已发送添加好友请求".into())
}

// ============================================================
// 删除好友
// ============================================================
#[tauri::command]
fn del_friend(to_id: i64) -> Result<String, String> {
    let user = TCP.current_user().ok_or("尚未登录")?;
    if to_id <= 0 {
        return Err("参数非法".into());
    }
    ensure_connected()?;

    let req = DelFriendRequest {
        from_id: user.id,
        to_id,
    };
    let mut payload = Vec::with_capacity(req.encoded_len());
    req.encode(&mut payload).map_err(|e| e.to_string())?;
    TCP.send_msg(10, payload).map_err(|e| e.to_string())?;
    Ok("已发送删除好友请求".into())
}

// ============================================================
// 获取好友列表（前端本地缓存兜底；服务端扩展后再切换到真实查询）
// ============================================================
#[tauri::command]
fn get_friends() -> Result<Vec<FriendInfo>, String> {
    let _user = TCP.current_user().ok_or("尚未登录")?;
    Ok(Vec::new())
}

// ============================================================
// 注销 / 登出
// ============================================================
#[tauri::command]
fn logout() -> Result<String, String> {
    let user = TCP.current_user().ok_or("尚未登录")?;
    ensure_connected().ok();

    let req = LoginRequest {
        id: user.id,
        username: user.name,
        password: String::new(),
    };
    let mut payload = Vec::with_capacity(req.encoded_len());
    req.encode(&mut payload).map_err(|e| e.to_string())?;
    let _ = TCP.send_msg(3, payload);

    TCP.disconnect();
    Ok("已注销".into())
}

// ============================================================
// 获取当前登录用户信息（前端刷新用）
// ============================================================
#[tauri::command]
fn get_me() -> Option<LoginUser> {
    TCP.current_user()
}

// ============================================================
// 检查网络连接状态
// ============================================================
#[tauri::command]
fn get_net_status() -> serde_json::Value {
    serde_json::json!({
        "connected": TCP.is_connected(),
        "server_addr": TCP.server_addr(),
    })
}

// ============================================================
// 设置服务端地址（允许前端自定义连接地址）
// ============================================================
#[tauri::command]
fn set_server(addr: String) -> Result<String, String> {
    TCP.disconnect();
    TCP.connect(&addr).map_err(|e| e.to_string())?;
    // set_server 直接调 connect，需要手动确保读线程启动
    TcpClient::ensure_reader(&TCP);
    Ok(format!("已连接到 {}", addr))
}

// ============================================================
// 辅助函数：确保已连接到服务端 + 后台读线程已启动
// ============================================================
fn ensure_connected() -> Result<(), String> {
    if !TCP.is_connected() {
        eprintln!("[ensure_connected] 未连接，开始连接 127.0.0.1:8080");
        TCP.connect("127.0.0.1:8080").map_err(|e| {
            eprintln!("[ensure_connected] 连接失败: {}", e);
            e.to_string()
        })?;
        eprintln!("[ensure_connected] 连接成功");
    }
    // 关键修复：确保读线程已启动——register / login / set_server
    //   等所有命令都能收到服务端推送的 ACK 消息
    TcpClient::ensure_reader(&TCP);
    Ok(())
}
