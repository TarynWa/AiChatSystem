add_executable(log_threadpool_test TestLogThreadPool.cpp)
add_executable(client Client.cpp)
target_link_libraries(log_threadpool_test
    PRIVATE
        lib::muduo_log
        lib::muduo_threadpool
        lib::muduo_chat
        muduo_net
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