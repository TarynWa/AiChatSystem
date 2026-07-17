#ifndef _SCHEDULE_THREAD_POOL_HPP_
#define _SCHEDULE_THREAD_POOL_HPP_
#include "SyncQueue3.hpp"
#include <thread>
#include <vector>
#include <functional>
#include <future>
#include <iostream>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
class ScheduleThreadPool{
    private:
    SyncQueue3 task_queue;
    std::list<std::shared_ptr<std::thread>> threads;
    std::atomic<bool> is_running_;
    std::once_flag once_flag;
    void Start(int numthreads){
        is_running_ = true;
        for(int i = 0; i < numthreads; i++){
            threads.push_back(std::make_shared<std::thread>(&ScheduleThreadPool::WorkerThread, this));
        }
    }
    void WorkerThread(){
        while(is_running_){
            PatrTask task;
            if(task_queue.Take(task)!=0){
                continue;
            }
            task.task();
        }
    }
    void StopGroupThread(){
        task_queue.Stop();
        is_running_ = false;
        for(auto& thread : threads){
            thread->join();
            thread.reset();
        }
        threads.clear();
    }
    public:
    ScheduleThreadPool(int numthreads, int max_size, int waittime_) : task_queue(max_size, waittime_), threads(numthreads) {}
    ~ScheduleThreadPool(){
        Stop();
    }
    void Start(int numthreads){
        std::call_once(once_flag, [this, numthreads](){Start(numthreads);});
    }
    void Stop(){
        std::call_once(once_flag, [this](){StopGroupThread();});
    }
    template<typename F, typename... Args>
    auto Submit(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>{
        using return_type = typename std::result_of<F(Args...)>::type;
        auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<return_type> res = task->get_future();
        if(task_queue.Put(PatrTask(0, [task](){(*task)();}))!=0){
            (*task)();
        }
        return res;
    }
};
#endif
