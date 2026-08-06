#include "Logger.hpp"

void Logger1::setOutput(LogOutputFunc out)
{
    output_ = out;
}

void Logger1::setFlush(FlushFunc flush)
{
    flush_ = flush;
}

void Logger1::setLogLevel(int level)
{
    logLevel_ = level;
}

int Logger1::GetLogLevel()
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

Logger1::LogOutputFunc Logger1::output_ = defaultOutput;
Logger1::FlushFunc Logger1::flush_ = defaultFlush;
int Logger1::logLevel_ = 1;
