add_library(muduo_threadpool INTERFACE)

target_include_directories(muduo_threadpool
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(muduo_threadpool INTERFACE cxx_std_17)

add_library(threadpool::muduo_threadpool ALIAS muduo_threadpool)
