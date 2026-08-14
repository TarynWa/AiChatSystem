// ============================================================
// proto.rs - Protobuf 生成代码导入
// ============================================================
// build.rs 调用 prost-build 编译 ../../chatsystem/chat.proto
// 生成的 Rust 代码位于 OUT_DIR/chat.rs，此处使用 include! 宏直接嵌入
// ============================================================

pub use prost::Message;

// 嵌入编译生成的 Rust protobuf 代码
include!(concat!(env!("OUT_DIR"), "/chat.rs"));

// ------------------------------------------------------------
// 把 EnMsgType 枚举的数值转成字符串别名，方便日志打印
// ------------------------------------------------------------
pub fn msg_type_name(t: i32) -> &'static str {
    match t {
        0 => "MSG_NONE",
        1 => "LOGIN_MSG",
        2 => "LOGIN_MSG_ACK",
        3 => "LOGINOUT_MSG",
        4 => "REG_MSG",
        5 => "REG_MSG_ACK",
        6 => "ONE_CHAT_MSG",
        7 => "ONE_CHAT_MSG_ACK",
        8 => "ADD_FRIEND_MSG",
        9 => "ADD_FRIEND_MSG_ACK",
        10 => "DEL_FRIEND_MSG",
        11 => "DEL_FRIEND_MSG_ACK",
        12 => "CREATE_GROUP_MSG",
        13 => "CREATE_GROUP_MSG_ACK",
        14 => "ADD_GROUP_MSG",
        15 => "ADD_GROUP_MSG_ACK",
        16 => "QUIT_GROUP_MSG",
        17 => "QUIT_GROUP_MSG_ACK",
        18 => "GROUP_CHAT_MSG",
        19 => "GROUP_CHAT_MSG_ACK",
        20 => "CROSS_NODE_CHAT_MSG",
        _ => "UNKNOWN",
    }
}
