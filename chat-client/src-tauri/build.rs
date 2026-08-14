// ============================================================
// build.rs - 编译时将 chat.proto 翻译为 Rust 模块 + Tauri 2 构建钩子
// ============================================================
// 职责：
//   1. 调用 prost-build 编译 ../../chatsystem/chat.proto
//      生成的 Rust 代码位于 OUT_DIR/chat.rs
//   2. 调用 tauri_build::build() 注入 Tauri 2 的构建期配置
//      （生成 capabilities 校验、context 模块等）
//   src/proto.rs 用 include!(concat!(env!("OUT_DIR"), "/chat.rs")) 导入
// ============================================================

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // 告诉 cargo：如果 chat.proto 内容变化，则重新执行 build.rs
    println!("cargo:rerun-if-changed=../../chatsystem/chat.proto");
    // tauri.conf.json / capabilities 变化时重新构建
    println!("cargo:rerun-if-changed=tauri.conf.json");

    // ---- 1. Protobuf 编译 ----
    prost_build::compile_protos(
        &["../../chatsystem/chat.proto"], // 输入 proto 文件列表
        &["../../chatsystem/"],           // proto include 搜索路径
    )?;

    // ---- 2. Tauri 2 构建钩子（必需） ----
    tauri_build::build();

    Ok(())
}
