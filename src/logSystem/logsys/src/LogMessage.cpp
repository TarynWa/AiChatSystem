#include "LogMessage.hpp"
namespace wangt
{
    LogMessage::LogMessage(const wangt::LOG_LEVEL &le, const string &filename, const string &funcname, const int line) : header_{}, text_{}, level_{le}
    {
        header_ = Timestamp::Now().toFormattedString() + " " + GetIndexTostr(level_) + " " + filename + " " + funcname + " " + to_string(line) + " ";
    }
}
wangt::LogMessage::~LogMessage()
{
}

const wangt::LOG_LEVEL &wangt::LogMessage::getlevel() const
{
    return this->level_;
}

const string wangt::LogMessage::toString() const
{
   return header_+text_;
}