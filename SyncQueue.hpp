#ifndef SYNC_QUEUE_HPP
#define SYNC_QUEUE_HPP
#include <queue>
#include <mutex>
#include <atomic>
#include <condition_variable>

template <typename T>
class SyncQueue {
    private:
        std::queue<T> queue_;
        mutable std::mutex mutex_;
        std::condition_variable m_not_empty_;
        std::condition_variable m_not_full_;
        size_t max_size_;
        size_t size_;
        std::atomic<bool> needstop_;
        bool isFull()const;
        bool isEmpty()const;
        template <typename F>
        void add(F&& x){
            std::unique_lock<std::mutex> lock(mutex_);
            m_not_full_.wait(lock, [this](){return  !isFull() || needstop_;});
            if(needstop_){
                return;
            }
            queue_.push(std::forward<F>(x));
            ++size_;
            m_not_empty_.notify_one();
        }
    public:
        SyncQueue(size_t max_size){
            max_size_ = max_size;
            size_ = 0;
            needstop_ = false;
        }
        ~SyncQueue(){
            if(!needstop_){
                stop();
            }
        }
        void put(const T& x){
            add(x);
        }
        void put(T&& x){
            add(std::move(x));
        }
        void take(T& x){
            std::unique_lock<std::mutex> lock(mutex_);
            m_not_empty_.wait(lock, [this](){return !isEmpty() || needstop_;});
            if(needstop_){
                return;
            }
            x = std::move(queue_.front());
            queue_.pop();
            --size_;
            m_not_full_.notify_one();
        }
        void take(std::queue<T>& q){
            std::unique_lock<std::mutex> lock(mutex_);
            m_not_empty_.wait(lock, [this](){return !isEmpty() || needstop_;});
            if(needstop_){
                return;
            }
            q = std::move(queue_);
            size_ = 0;
            m_not_full_.notify_one();
        }
        bool empty()const{
            return isEmpty();
        }
        bool full()const{
            return isFull();
        }
        void stop(){
            {
                std::unique_lock<std::mutex> lock(mutex_);
                needstop_ = true;
            }
            m_not_empty_.notify_all();
            m_not_full_.notify_all();
        }
};
#endif

template <typename T>
inline bool SyncQueue<T>::isFull() const
{
    std::unique_lock<std::mutex> lock(mutex_);
    if(size_ >= max_size_)
        return true;    
}

template <typename T>
inline bool SyncQueue<T>::isEmpty() const
{
    std::unique_lock<std::mutex> lock(mutex_);
    if(size_ == 0)
        return true;
}