set(CHAT_SOURCES
   net/chatserver.cpp
   service/chatservice.cpp
   proto/chat.pb.cc
   storage/mysql.cpp
   model/UserModel.cpp
   util/PwdUtils.cpp
   model/FriendModel.cpp
   model/GroupModel.cpp
   model/OfflineMsgModel.cpp
   storage/RedisMgr.cpp
)

add_library(muduo_chat STATIC ${CHAT_SOURCES})

target_include_directories(muduo_chat
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(muduo_chat PUBLIC cxx_std_17)

# OpenSSL::Crypto 用于密码SHA256哈希与随机盐生成（PwdUtils依赖）
find_package(OpenSSL REQUIRED)

# hiredis 用于Redis Pub/Sub跨节点消息同步
find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h)
find_library(HIREDIS_LIBRARY hiredis)

# 网络层已切换为自研 NetWorkLibrary（nwl），回调签名与 muduo 同构（见 net/nwl_bridge.hpp）
target_link_libraries(muduo_chat PUBLIC lib::nwl lib::muduo_log mysqlclient OpenSSL::Crypto ${HIREDIS_LIBRARY})

add_library(lib::muduo_chat ALIAS muduo_chat)
