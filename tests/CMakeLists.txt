add_executable(log_threadpool_test TestLogThreadPool.cpp)
add_executable(client Client.cpp)

# 服务端已改用自研网络库 nwl（不再链接 muduo_net）；
# muduo_base 仅为 chatservice 异步 DB 池（muduo::ThreadPool）提供实现，属基础库非网络层
target_link_libraries(log_threadpool_test
    PRIVATE
        lib::muduo_chat
        lib::muduo_threadpool
        muduo_base
        pthread
        protobuf
)

target_link_libraries(client
    PRIVATE
        lib::muduo_log
        lib::muduo_chat
        protobuf
)

add_test(NAME LogThreadPoolTest COMMAND log_threadpool_test)
add_test(NAME client COMMAND client)