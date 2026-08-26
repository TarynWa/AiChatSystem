#ifndef SYNCQUEUE1_HPP
#define SYNCQUEUE1_HPP
#include <list>
#include <mutex>
#include <atomic>
#include <condition_variable>
template <typename T>
class SyncQueue1
{
private:
    std::list<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable m_not_empty_;
    std::condition_variable m_not_full_;
    size_t max_size_;
    size_t size_;
    size_t m_waittime_;
    std::atomic<bool> needstop_;
    bool isFull()
    {
        if (size_ >= max_size_)
        {
            return true;
        }
        return false;
    }
    bool isEmpty()
    {
        if (size_ == 0)
        {
            return true;
        }
        return false;
    }
    template <typename F>
    int add(F &&x)
    {
        std::unique_lock<std::mutex> locker(mutex_);
        if (!m_not_full_.wait_for(locker, std::chrono::seconds(m_waittime_, [this]
                                                               { return needstop_ || !isFull() })))
        {
            return 1;
        }
        if (needstop_)
        {
            return 2;
        }
        queue_.push_back(std::forward<F>(x));
        size_++;
        m_not_empty_.notify_one();
        return 0;
    }

public:
    SyncQueue1(size_t max_size)
    {
        max_size_ = max_size;
        size_ = 0;
        needstop_ = false;
    }
    ~SyncQueue1()
    {
        if (!needstop_)
        {
            stop();
        }
    }
    void put(const T &x)
    {
        add(x);
    }
    void put(T &&x)
    {
        add(std::move(x));
    }
    void take(T &x)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        m_not_empty_.wait(lock, [this]()
                          { return !isEmpty() || needstop_; });
        if (needstop_)
        {
            return;
        }
        x = std::move(queue_.front());
        queue_.pop();
        --size_;
        m_not_full_.notify_one();
    }
    void take(std::queue<T> &q)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        m_not_empty_.wait(lock, [this]()
                          { return !isEmpty() || needstop_; });
        if (needstop_)
        {
            return;
        }
        q = std::move(queue_);
        size_ = 0;
        m_not_full_.notify_one();
    }
    bool empty() const
    {
        return isEmpty();
    }
    bool full() const
    {
        return isFull();
    }
    void stop()
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            needstop_ = true;
        }
        m_not_empty_.notify_all();
        m_not_full_.notify_all();
    }
    size_t size()
    {
        return queue_.size();
    }
};
#endif
