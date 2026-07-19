add_executable(log_threadpool_test TestLogThreadPool.cpp)

target_link_libraries(log_threadpool_test
    PRIVATE
        log::muduo_log
        threadpool::muduo_threadpool
)

add_test(NAME LogThreadPoolTest COMMAND log_threadpool_test)
