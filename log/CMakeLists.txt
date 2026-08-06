set(LOG_SOURCES
    AppendFile.cpp
    Asynclogging.cpp
    LogFile.cpp
    Logger.cpp
    TimeStamp.cpp
)

add_library(muduo_log STATIC ${LOG_SOURCES})

target_include_directories(muduo_log
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(muduo_log PUBLIC cxx_std_17)

add_library(lib::muduo_log ALIAS muduo_log)
