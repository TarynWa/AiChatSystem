#ifndef LOGGER_HPP
#define LOGGER_HPP
#include "LogMessage.hpp"
#include <functional>
class Logger
{
private:
    LogMessage logMessage_;

public:
    using LogOutputFunc = std::function<void(const std::string &)>;
    using FlushFunc = std::function<void()>;
    static LogOutputFunc output_;
    static FlushFunc flush_;
    static int logLevel_ ;
    Logger(int level, const char *file, int line, const char *func)
        : logMessage_(Timestamp::Now(), level, "", line, func)
    {
        // 这里不再需要给 logMessage_ 赋值了
        logLevel_ = level;
    }
    ~Logger(){
        logMessage_<<"\n";
        if (logMessage_.getmsg().empty()) {
            return;
        }
        output_(logMessage_.getmsg());
        if (logMessage_.getmsg().find("FATAL") != std::string::npos) {
            flush_();
            abort();
        }  
    }
    LogMessage &stream()
    {
        return logMessage_;
    }
    static void setOutput(LogOutputFunc out);
    static void setFlush(FlushFunc flush);
    static void setLogLevel(int level);
    static int GetLogLevel();
};
#endif // LOGGER_HPP

#define LOG_TRACE Logger(TRACE, __FILE__, __LINE__, __func__).stream()
#define LOG_DEBUG Logger(DEBUG, __FILE__, __LINE__, __func__).stream()
#define LOG_INFO Logger(INFO, __FILE__, __LINE__, __func__).stream()
#define LOG_WARN Logger(WARN, __FILE__, __LINE__, __func__).stream()
#define LOG_ERROR Logger(ERROR, __FILE__, __LINE__, __func__).stream()
#define LOG_FATAL Logger(FATAL, __FILE__, __LINE__, __func__).stream()