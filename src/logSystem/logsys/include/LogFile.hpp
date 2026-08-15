#ifndef LOGFILE_HPP
#define LOGFILE_HPP
#include "AppendFile.hpp"
#include<mutex>
#include <unistd.h>
namespace wangt
{
    class LogFile
    {
    private:
        const string baseName_;
        const size_t rollSize_;

        time_t startOfPeriod_; // 2025,10,13 20 21 30
        time_t lastRoll_;
        time_t lastFlush_;
        static const size_t kRollPerSecods_ = 60 * 60 * 24;// roll 的周期：即60*60*24 =1day

        const int flushInterval_;
        const int chechEventN_;
        int count_;
        std::unique_ptr<std::mutex> mutex_;
        std::unique_ptr<wangt::AppendFile> file_;
    private:
        // 不加锁的append方式
        void append_unlocked(const char *logline, const int len);
        // 获取日志文件的名称
        static string getLogFileName(const string &basename, const Timestamp &now);

    public:
        LogFile(const string &basename, // 日志文件basename
                size_t rollSize,        // 日志文件达到rollsize生成一个新文件
                bool threadSafe = true, // 线程安全控制项, 默认为true.
                // 当只有一个后端AsnycLogging和后端线程时,
                // 该项可置为false
                int flushInterval = 3,   // 日志写入间隔时间 秒级
                int checkEveryN = 1024); // 每调用checkEveryN_次日志写，就滚动一次
        ~LogFile();
        void append(const std::string &info);
        void append(const char *info, const int len);
        void flush();
        bool rollFile();
};
}
#endif