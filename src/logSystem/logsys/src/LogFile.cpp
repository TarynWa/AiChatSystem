#include "LogFile.hpp"
namespace wangt
{
    void LogFile::append_unlocked(const char *logline, const int len)
    {
        file_->append(logline, len);
        if (file_->wittenBytes() >= rollSize_)
        {
            rollFile();
        }
        else
        {
            ++count_;
            if (count_ > chechEventN_)
            {
                count_ = 0;
                time_t now = ::time(NULL);
                time_t thisPeriod_ = now / kRollPerSecods_ *
                                     kRollPerSecods_;
                if (thisPeriod_ != startOfPeriod_)
                {
                    rollFile();
                }
                else if (now - lastFlush_ > flushInterval_)
                {
                    lastFlush_ = now;
                    file_->flush();
                }
            }
        }
    }
    const string hostname()
    {
        char buff[128] = {0};
        if (!::gethostname(buff, 128))
        {
            return string(buff);
        }
        return string("unknownamehost");
    }
    pid_t pid()
    {
        return getpid();
    }
    string LogFile::getLogFileName(const string &basename, const Timestamp &now)
    {
        string filename = basename;
        filename += ".";
        filename += now.toFormattedFile();
        filename += ".";
        filename += hostname();
        filename += ".";
        filename += to_string(pid());
        filename += ".log";
        return filename;
    }
    LogFile::LogFile(const string &basename, size_t rollSize, bool threadSafe, int flushInterval, int checkEveryN) : baseName_(basename), rollSize_(rollSize), flushInterval_(flushInterval), chechEventN_(checkEveryN), count_(0), lastFlush_(0), startOfPeriod_(0), lastRoll_(0), mutex_(threadSafe ? new mutex{} : nullptr)
    {
        rollFile();
    }
    LogFile::~LogFile()
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
            std::unique_lock<std::mutex> lock(*mutex_);
            append_unlocked(info,len);
        }
        else
        {
            append_unlocked(info, len);
        }
    }
    void LogFile::flush()
    {
        file_->flush();
    }
    bool LogFile::rollFile()
    {
        Timestamp t1;
        t1.now();
        string filename = getLogFileName(baseName_, t1);
        time_t start = (t1.getsecond() / kRollPerSecods_) * kRollPerSecods_;
        if (t1.getsecond() > lastRoll_)
        {
            lastRoll_ = t1.getsecond();
            lastFlush_ = t1.getsecond();
            startOfPeriod_ = start;
            //cout << filename << endl;
            file_.reset(new wangt::AppendFile(filename));
            return true;
        }
        return false;
    }
}