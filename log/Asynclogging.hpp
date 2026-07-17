#ifndef ASYNCLOGGING_HPP
#define ASYNCLOGGING_HPP
#include "LogFile.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
class AsyncLogging
{
    private:
    AsyncLogging(const AsyncLogging&)=delete;
    AsyncLogging& operator=(const AsyncLogging&)=delete;
    void workerThread();
    const int flushInterval_;
    std::atomic<bool> running_;
    std::string basename_;
    const size_t rollSize_;

    std::unique_ptr<std::thread> thread_;
    std::mutex mutex_;
    std::condition_variable cond_;

    std::string currentBuffer_;
    std::vector<std::string> buffers_;

    LogFile output_;
    public:
    AsyncLogging(const std::string& basename, size_t rollSize, int flushInterval = 3);
    ~AsyncLogging();
    void append(const std::string& logline);
    void append(const char* logline, int len);
    void stop();
    void start();
    void flush();
};
#endif // ASYNCLOGGING_HPP