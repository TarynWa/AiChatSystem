#include "Logger.hpp"

void Logger::setOutput(LogOutputFunc out)
{
    output_ = out;
}

void Logger::setFlush(FlushFunc flush)
{
    flush_ = flush;
}

void Logger::setLogLevel(int level)
{
    logLevel_ = level;
}

int Logger::GetLogLevel()
{
    return logLevel_;
}


void defaultOutput(const std::string &msg)
{
    size_t n = fwrite(msg.c_str(), 1, msg.size(), stdout);
}

void defaultFlush()
{
    // fflush(stdout);
}

Logger::LogOutputFunc Logger::output_ = defaultOutput;
Logger::FlushFunc Logger::flush_ = defaultFlush;
int Logger::logLevel_ = 1;
