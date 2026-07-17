#include "Asynclogging.hpp"

void AsyncLogging::workerThread()
{
    std::vector<std::string> newBuffers;
    newBuffers.reserve(16);
    while (running_)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (buffers_.empty())
            {
                cond_.wait_for(lock, std::chrono::seconds(flushInterval_));
            }
            buffers_.push_back(std::move(currentBuffer_));
            currentBuffer_.clear();
            newBuffers.swap(buffers_);
        }
        for (const auto &buffer : newBuffers)
        {
            output_.append(buffer);
        }
        if (!newBuffers.empty())
        {
            output_.flush();
        }
        newBuffers.clear();
    }
    output_.flush();
}

AsyncLogging::AsyncLogging(const std::string &basename, size_t rollSize, int flushInterval):
    flushInterval_(flushInterval),
    running_(false),
    basename_(basename),
    rollSize_(rollSize),
    output_(basename, rollSize, false, flushInterval, 1024)
{
    currentBuffer_.reserve(1024 * 1024);
    buffers_.reserve(16);
}

AsyncLogging::~AsyncLogging()
{
    if (running_)
    {
        stop();
    }
}

void AsyncLogging::append(const std::string &logline)
{
    append(logline.c_str(), logline.size());
}

void AsyncLogging::append(const char *logline, int len)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (currentBuffer_.size() + len < currentBuffer_.capacity())
    {
        currentBuffer_.append(logline, len);
    }
    else
    {
        buffers_.push_back(std::move(currentBuffer_));
        currentBuffer_.clear();
        currentBuffer_.append(logline, len);
        cond_.notify_one();
    }
}

void AsyncLogging::stop()
{
    running_ = false;
    cond_.notify_one();
    if (thread_ && thread_->joinable())
    {
        thread_->join();
    }
}

void AsyncLogging::start()
{
    running_ = true;
    thread_.reset(new std::thread(&AsyncLogging::workerThread, this));
}

void AsyncLogging::flush()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!currentBuffer_.empty())
    {
        buffers_.push_back(std::move(currentBuffer_));
        currentBuffer_.clear();
        cond_.notify_one();
    }
}