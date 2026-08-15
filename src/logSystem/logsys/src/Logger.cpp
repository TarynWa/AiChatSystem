#include "Logger.hpp"
namespace wangt
{
    void Logger::setOutput(Outputfunc out)
    {
        output_ = out;
    }
    void Logger::setFlush(Flushfunc flush)
    {
        flush_ = flush;
    }

    Logger::Logger(const LOG_LEVEL &le, const string &filename, const string &funcname, const int line) : impl_(le, filename, funcname, line)
    {
    }
    Logger::~Logger()
    {
        impl_ << '\n';
        //output_(impl_.toString());
        output_(impl_.toString());
        // string c=impl_.toString();
        // cout<<c;
        flush_();
        if (impl_.getlevel() == LOG_LEVEL::FATAL)
        {
            fprintf(stderr, "Process exit \n");
            exit(EXIT_FAILURE);
        }
    }
    LogMessage &Logger::stream()
    {
        // TODO: 在此处插入 return 语句
        return impl_;
    }
    void Logger::setLogLevel(LOG_LEVEL level)
    {
        s_level_ = level;
    }
    LOG_LEVEL Logger::GetLogLevel()
    {
        return s_level_;
    }
    LOG_LEVEL initLogLevel()
    {
        if (::getenv("WANGT::LOG_TRACE"))
        {
            return LOG_LEVEL::TRACE;
        }
        else if (::getenv("WANGT::LOG_DEBUG"))
        {
            return LOG_LEVEL::DEBUG;
        }
        else
        {
            return LOG_LEVEL::INFO;
        }
    }
    void defaultOutput(const string &msg)
    {
        size_t n = fwrite(msg.c_str(), 1, msg.size(), stdout);
    }

    void defaultFlush()
    {
        //fflush(stdout);
    }
    Logger::Outputfunc Logger::output_ = defaultOutput;
    Logger::Flushfunc Logger::flush_ = defaultFlush;
    LOG_LEVEL Logger::s_level_ = initLogLevel();

}
