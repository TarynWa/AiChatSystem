#include "AsyncLogging.hpp"

namespace wangt
{
    static const int BufMaxLen = 4000;
    static const int BufQueueSize = 16;
    AsyncLogging::AsyncLogging(const std::string &basename, size_t rollSize, int flushInterval)
        : flushInterval_(flushInterval), // 刷新间隔
          running_(false),
          rollSize_(rollSize),
          pthread_(nullptr),
          // latch_(1),
          output_(basename, rollSize, false) // 定义日志文件对象
    {
        currentBuffer_.reserve(BufMaxLen);
        buffers_.reserve(BufQueueSize); // vector预定大小，避免自动增长（效率更高）
    }
    AsyncLogging::~AsyncLogging()
    {
        if (running_)
        {
            stop();
        }
        flush();
    }
    void AsyncLogging::start()
    {
        running_ = true;
        // 执行该异步日志记录器的线程
        pthread_.reset(new std::thread(&AsyncLogging::workthreadfunc, this));
        // latch_.wait();
    }
    void AsyncLogging::stop()
    {
        running_ = false;
        cond_.notify_all();
        pthread_->join();
        output_.flush();
    }
    void AsyncLogging::append(const string &info)
    {
        append(info.c_str(), info.size());
    }
    void AsyncLogging::append(const char *info, int len)
    {
        std::unique_lock<std::mutex> _lock(mutex_);
        if (currentBuffer_.size() >= BufMaxLen ||
            (currentBuffer_.capacity() - currentBuffer_.size()) < len)
        {
            buffers_.push_back(std::move(currentBuffer_));
             currentBuffer_.clear();
            currentBuffer_.reserve(BufMaxLen);
        }
        else
        {
            currentBuffer_.append(info, len);
        }
        cond_.notify_all();
    }

    void AsyncLogging::workthreadfunc()
    {
        // tulun::LogFile output(basename_,rollSize_,false); //定义日志文件对象
        std::vector<std::string>
            buffersToWrite; // 线程函数的局部队列
        // latch_.countDown();
        while (running_)
        {
            // std::this_thread::sleep_for(std::chrono::milliseconds(5000));
            {
                std::unique_lock<std::mutex> _lock(mutex_);
                if (buffers_.empty())
                {
                    cond_.wait_for(_lock,
                                   std::chrono::seconds(flushInterval_));
                    // 时间点到达 ，还要获得mutex_ 才能从wait_for 函数返回；
                }
                // 无论cond是因何（一是超时，二是当前缓冲区写满了）而醒来，都要将currentBuffer_放到buffers_中。 测试
                // 如果是因为时间到（3秒）而醒，那么currentBuffer_还没满，此时也要将之写入LogFile中。
                // 如果已经有一个前端buffer满了，那么在前端线程中就已经把一个前端buffer放到buffers_中了。
                // 此时，还是需要把currentBuffer_放到buffers_中（注意，前后放置是不同的buffer，
                // 因为在前端线程中，currentBuffer_已经被换成nextBuffer_指向的 buffer了）。
                buffers_.push_back(std::move(currentBuffer_));
                currentBuffer_.reserve(BufMaxLen);
                buffersToWrite.swap(buffers_);
                buffers_.reserve(BufQueueSize);
                // 释放mutex_ ;
            }
            ////异步写文件
            // 如果将要写入文件的buffer列表中buffer的个数大于25，那么将多余数据删 除。
            // 前端陷入死循环，拼命发送日志消息，超过后端的处理能力，这是典型的生产速 度超过消费速度，
            // 会造成数据在内存中的堆积，严重时引发性能问题(可用内存不足)或程序崩溃(分配内存失败)。
            if (buffersToWrite.size() > 25)
            {
                char buf[256];
                snprintf(buf, sizeof buf, "Dropped log messages at largerbuffers\n");
                fputs(buf, stderr);
                // 丢掉多余日志，以腾出内存，仅保留两块缓冲区
                buffersToWrite.erase(buffersToWrite.begin() + 2,
                                     buffersToWrite.end());
            }
            // 将buffersToWrite的数据写入到日志文件中
            for (const auto &buffer : buffersToWrite)
            {
                output_.append(buffer.c_str(), buffer.size());
            }
            buffersToWrite.clear();
        }
        output_.flush();
    }
    void AsyncLogging::flush()
    {
        std::vector<std::string> buffersToWrite; //
        std::unique_lock<std::mutex> _lock(mutex_);
        buffers_.push_back(std::move(currentBuffer_));
        buffersToWrite.swap(buffers_);
        for (const auto &buffer : buffersToWrite)
        {
            output_.append(buffer.c_str(), buffer.size());
        }
        output_.flush();
        buffersToWrite.clear();
    }
}

/********************************************************************
Description :
前端在生成一条日志消息时，会调用AsyncLogging::append()。
如果currentBuffer_够用，就把日志内容写入到currentBuffer_中，
如果不够用(就认为其满了)，就把currentBuffer_放到已满buffer_数组中，
等待消费者线程（即后台线程）来取。则将预备好的另一块缓冲
（nextBuffer_）移用为当前缓冲区（currentBuffer_）。
*******************************************************************/
