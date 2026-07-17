#include "LogFile.hpp"

void LogFile::append_unlocked(const char *logline, const int len)
{
    file_->append(logline, len);
    if (file_->wittenBytes() > rollSize_)
    {
        rollFile();
    }
    else
    {
        ++count_;
        if (count_ >= chechEventN_)
        {
            count_ = 0;
            time_t now = ::time(nullptr);
            time_t thisPeriod = now / kRollPerSecods_ * kRollPerSecods_;
            if (thisPeriod != startOfPeriod_)
            {
                rollFile();
            }
            else if (now - lastFlush_ > flushInterval_)
            {
                lastFlush_ = now;
                flush();
            }
        }
    }
}

std::string LogFile::getLogFileName(const std::string &basename, const Timestamp &now)
{
    std::string filename = basename;
        filename += ".";
        filename += now.toFormattedFile();
        filename += ".log";
        return filename;
}

LogFile::LogFile(const std::string &basename, size_t rollSize, bool threadSafe, int flushInterval, int checkEveryN)
: baseName_(basename),
  rollSize_(rollSize),
  flushInterval_(flushInterval),
  chechEventN_(checkEveryN),
  count_(0),
  mutex_(threadSafe ? new std::mutex : nullptr),
  file_(new AppendFile(getLogFileName(baseName_, Timestamp::Now())))
{
}

void LogFile::append(const std::string &info)
{
    append(info.c_str(), info.size());
}

void LogFile::append(const char *info, const int len)
{
    if (mutex_)
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        append_unlocked(info, len);
    }
    else
    {
        append_unlocked(info, len);
    }
}

void LogFile::flush()
{
    if (mutex_)
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        file_->flush();
    }
    else
    {
        file_->flush();
    }
}

bool LogFile::rollFile()
{
    time_t now = ::time(nullptr);
    std::string filename = getLogFileName(baseName_, Timestamp::Now());
    time_t start = now / kRollPerSecods_ * kRollPerSecods_;
    if (now > lastRoll_)
    {
        lastRoll_ = now;
        lastFlush_ = now;
        startOfPeriod_ = start;
        file_.reset(new AppendFile(filename));
        return true;
    }
    return false;
}
