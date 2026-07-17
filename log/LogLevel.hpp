#ifndef LOG_LEVEL_HPP
#define LOG_LEVEL_HPP
#define TRACE    0
#define DEBUG    1
#define INFO     2
#define WARN     3
#define ERROR    4
#define FATAL    5

   static inline const char* levelStr[] = {
        "TRACE",
        "DEBUG",
        "INFO",
        "WARN",
        "ERROR",
        "FATAL"
    };



#endif // LOG_LEVEL_HPP