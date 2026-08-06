set(CHAT_SOURCES
   chatserver.cpp
   chatservice.cpp
   chat.pb.cc
)

add_library(muduo_chat STATIC ${CHAT_SOURCES})

target_include_directories(muduo_chat
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(muduo_chat PUBLIC cxx_std_17)

add_library(lib::muduo_chat ALIAS muduo_chat)
