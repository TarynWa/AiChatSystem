add_executable(log_threadpool_test TestLogThreadPool.cpp)

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

add_test(NAME LogThreadPoolTest COMMAND log_threadpool_test)
