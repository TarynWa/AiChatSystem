set(CHAT_SOURCES
   chatserver.cpp
   chatservice.cpp
   chat.pb.cc
   mysql.cpp
   UserModel.cpp
)

add_library(muduo_chat STATIC ${CHAT_SOURCES})

target_include_directories(muduo_chat
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(muduo_chat PUBLIC cxx_std_17)

target_link_libraries(muduo_chat PUBLIC lib::muduo_log mysqlclient)

add_library(lib::muduo_chat ALIAS muduo_chat)
