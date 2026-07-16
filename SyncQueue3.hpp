#ifndef _SYNC_QUEUE3_HPP_
#define _SYNC_QUEUE3_HPP_
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
using task_t = std::function<void()>;
struct PatrTask{
    size_t first;
    task_t task;
    PatrTask(size_t first, task_t task): first(first), task(task) {}
    PatrTask() : first(0), task(nullptr) {}
};
class SyncQueue3{
    private:
    std::priority_queue<PatrTask, std::vector<PatrTask>, std::greater<PatrTask>> tasks;
    size_t max_size;
    mutable std::mutex mtx;
    std::condition_variable m_not_full;
    std::condition_variable m_not_empty;
    size_t waittime_;
    bool is_running_;
    bool IsFull() const { return tasks.size() >= max_size; }
    bool IsEmpty() const { return tasks.empty(); }
    int add(const PatrTask& task){
        std::unique_lock<std::mutex> lock(mtx);
        bool waitret = m_not_full.wait_for(lock, std::chrono::milliseconds(waittime_), [this](){return !IsFull()||!is_running_;});
        if(waitret){
            return 1;
        }
        if(!is_running_){
            return 2;
        }
        tasks.push(task);
        m_not_empty.notify_one();
        return 0;
    }
    public:
    SyncQueue3(size_t max_size, size_t waittime_ = 1) : max_size(max_size), waittime_(waittime_), is_running_(true) {}
    ~SyncQueue3(){}
    int Put(const PatrTask& task){
        return add(task);
    }
    int Take(PatrTask& task){
        std::unique_lock<std::mutex> lock(mtx);
        m_not_empty.wait(lock, [this](){return !IsEmpty()||!is_running_;});
        if(!is_running_){
            return 2;
    }
    task = tasks.top();
    tasks.pop();
    while(std::cv_status::timeout != m_not_full.wait_for(lock,std::chrono::seconds(task.first)));
    m_not_full.notify_one();
    return 0;
}
    void Stop(){
        std::unique_lock<std::mutex> lock(mtx);
        is_running_ = false;
        while(!IsEmpty()){
            m_not_full.wait(lock);
        }
        m_not_full.notify_all();
        m_not_empty.notify_all();
    }
};
#endif
