// ============================================================
// main.rs - Tauri 二进制入口（极薄）
//   真正的业务/UI/TCP 代码全部在 lib.rs / tcp.rs / proto.rs 中
// ============================================================

#![cfg_attr(
    all(not(debug_assertions), target_os = "windows"),
    windows_subsystem = "windows"
)]

fn main() {
    chat_client_lib::run();
}
