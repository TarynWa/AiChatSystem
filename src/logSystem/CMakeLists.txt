set(LOG_SOURCES
    ./logsys/src/AppendFile.cpp
    ./logsys/src/AsyncLogging.cpp
    ./logsys/src/LogFile.cpp
    ./logsys/src/LogMessage.cpp
    ./logsys/src/Logger.cpp
    ./logsys/src/Timestamp.cpp
)

add_library(muduo_log STATIC ${LOG_SOURCES})

target_include_directories(muduo_log
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/logsys/include
)

target_compile_features(muduo_log PUBLIC cxx_std_17)

add_library(lib::muduo_log ALIAS muduo_log)