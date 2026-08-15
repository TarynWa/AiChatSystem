#ifndef LOGGER_HPP
#define LOGGER_HPP
#include<functional>
#include"LogMessage.hpp"
namespace wangt{
class Logger{
    public:
    using Outputfunc=function<void(const string&msg)>;
    using Flushfunc=function<void()>;
    static Outputfunc output_;
    static Flushfunc flush_;
    private:
    LogMessage impl_;//日志消息对象
    static LOG_LEVEL s_level_;
    public:
        static void setOutput(Outputfunc out);
        static void setFlush(Flushfunc flush);
        Logger(const LOG_LEVEL&le,const string &filename,const string &funcname,const int line);
        ~Logger();
        LogMessage&stream();
        static void setLogLevel(LOG_LEVEL level);
        static LOG_LEVEL GetLogLevel();
    };
    LOG_LEVEL initLogLevel();
}
#define WT_LOG_TRACE                                                \
    if (wangt::Logger::GetLogLevel() <= wangt::LOG_LEVEL::TRACE) \
    wangt::Logger(wangt::LOG_LEVEL::TRACE, __FILE__, __func__, __LINE__).stream()
#define WT_LOG_DEBUG                                                \
    if (wangt::Logger::GetLogLevel() <= wangt::LOG_LEVEL::DEBUG) \
    wangt::Logger(wangt::LOG_LEVEL::DEBUG, __FILE__, __func__, __LINE__).stream()
#define WT_LOG_INFO                                                \
    if (wangt::Logger::GetLogLevel() <= wangt::LOG_LEVEL::INFO) \
    wangt::Logger(wangt::LOG_LEVEL::INFO, __FILE__, __func__, __LINE__).stream()
#define WT_LOG_WARN                                                \
    if (wangt::Logger::GetLogLevel() <= wangt::LOG_LEVEL::WARN) \
    wangt::Logger(wangt::LOG_LEVEL::WARN, __FILE__, __func__, __LINE__).stream()
#define WT_LOG_ERROR                                                \
    if (wangt::Logger::GetLogLevel() <= wangt::LOG_LEVEL::ERROR) \
    wangt::Logger(wangt::LOG_LEVEL::ERROR, __FILE__, __func__, __LINE__).stream()
#define WT_LOG_FATAL                                                \
    if (wangt::Logger::GetLogLevel() <= wangt::LOG_LEVEL::FATAL) \
    wangt::Logger(wangt::LOG_LEVEL::FATAL, __FILE__, __func__, __LINE__).stream()
#endif 