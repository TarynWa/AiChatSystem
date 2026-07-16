#ifndef WorkThreadPool_hpp
#define WorkThreadPool_hpp
#include "SyncQueue2.hpp"
#include <thread>
#include <vector>
#include <iostream>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>

using task = std::function<void()>;
class WorkThreadPool{
    size_t m_numThreads_;
    SyncQueue2<task> m_taskQueue_; 
    std::vector<std::shared_ptr<std::thread>> m_threads_;
    std::atomic<bool> m_isRunning_;
    std::once_flag m_onceFlag_;
    void start(int numThreads){
        for(int i = 0; i < numThreads; i++){
            m_threads_.push_back(std::make_shared<std::thread>(&WorkThreadPool::RunInThread, this, i));
        }
    }
     int threadindex(){
        static  int threadIndex = 0;
        return threadIndex++%m_numThreads_;
    }
    void RunInThread(int threadIndex){
        while(m_isRunning_){
            std::list<task> tasks;
            if(m_taskQueue_.Take(tasks, threadIndex) == 0){
                for(auto& task : tasks){
                    task();
                }
            }
        else{
            int i = threadindex();
            if(i!= threadIndex &&m_taskQueue_.Take(tasks, i) == 0){
                for(auto& task : tasks){
                    task();
                }
            }
        }
    }
}
    void StopThreadGroup(){
        m_taskQueue_.Stop();
        for(auto& thread : m_threads_){
            thread->join();
        }
        m_threads_.clear();
        m_isRunning_ = false;
    }
    public:
    WorkThreadPool(const size_t numThreads, const size_t maxTaskSize, const size_t bucketSize) : m_numThreads_(numThreads), m_taskQueue_(maxTaskSize, bucketSize), m_isRunning_(true) {
        start(numThreads);
    }
    ~WorkThreadPool(){
        Stop();
    }
    void Stop(){
        std::call_once(m_onceFlag_, [this](){StopThreadGroup();});
    }
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))>{
        using returnType = decltype(f(args...));
        auto task = std::make_shared<std::packaged_task<returnType()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        auto future = task->get_future();
        if(m_taskQueue_.Put(std::move([task](){(*task)();}), threadIndex()) != 0){
            (*task)();
        }
        return future;
    }
};
#endif // WorkThreadPool_hpp