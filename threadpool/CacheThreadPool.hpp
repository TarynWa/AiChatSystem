#ifndef CACHETHREADPOOL_HPP
#define CACHETHREADPOOL_HPP
#include<mutex>
#include<unordered_map>
#include<functional>
#include<future>
#include<atomic>
#include <chrono>
#include"SyncQueue1.hpp"
int MaxTaskCount = 2;
const int KeepAliveTime = 10;
using task = std::function<void(void)>;
class CacheThreadPool{
    private:
    SyncQueue1<task>m_queue_;
    std::unordered_map<std::thread::id,std::shared_ptr<std::thread>>m_threadgroup;
    int m_coreThreadSize; //核心线程数
    int m_maxThreadSize; //最大线程数
    std::atomic_int m_idThreadSize; // 空闲线程数
    std::atomic_int m_curThreadSize; //当前线程池里面的线程总数
    mutable std::mutex mutex_;
    std::atomic_bool m_running;
    std::once_flag m_flag;
    void Start(int size){
        m_running = true;
        m_curThreadSize = size;
        for(int i = 0; i<size;++i){
            auto tha = std::make_shared<std::thread>(std::thread(&CacheThreadPool::RunInThread,this));
            std::thread::id tid = tha->get_id();
            m_threadgroup.emplace(tid,std::move(tha));
            m_idThreadSize++;
        }
    }    
    void RunInThread(){
        auto tid = std::this_thread::get_id();
        auto starttime = std::chrono::high_resolution_clock().now();
        while(m_running){
            task task;
            if(m_queue_.empty()&&m_queue_.size()==0){
                auto now = std::chrono::high_resolution_clock().now();
                auto intervalTime = std::chrono::duration_cast<std::chrono::seconds>(now-starttime);
                std::unique_lock<std::mutex>locker(mutex_);
                if(intervalTime.count()>=KeepAliveTime&&m_curThreadSize>m_coreThreadSize){
                    m_threadgroup.find(tid)->second->detach();
                    m_threadgroup.erase(tid);
                    m_curThreadSize--;
                    m_idThreadSize++;
                    return;
                }
            }
            if(!m_queue_.take(task)&&m_running){
                m_idThreadSize--;
                task();
                m_idThreadSize++;
                starttime = std::chrono::high_resolution_clock().now();
            }
        }
    }                
    void StopThreadGroup(){
        m_queue_.stop();
        m_running = false;
        for(auto& thread : m_threadgroup){
            thread.second->join();
        }
        m_threadgroup.clear();
    }         
    public:
    CacheThreadPool(int initNumThreads = 8 , int taskPoolsize = MaxTaskCount):m_coreThreadSize(initNumThreads),m_maxThreadSize(2*std::thread::hardware_concurrency()+1),m_idThreadSize(0),m_curThreadSize(0),m_queue_(taskPoolsize),m_running(false){
        Start(m_coreThreadSize);
    }
    void Stop(){
        std::call_once(m_flag,[this]{StopThreadGroup();});
    }

    template<class Func , class... Args>
    void execute(Func &&func , Args&& ...args)
    {
         auto task = std::make_shared<std::function<void()>>(std::bind(func,std::forward<Args>(args)...));
         if(m_queue_.put([task](){(*task)();})!=0){
            (*task)();
         }
    }
    template<class Func , class... Args>
    auto submit(Func&&func,Args&&... args)->std::future<decltype(func(args...))>{
        using RecType = decltype(func(arg...));
        auto task = std::make_shared<std::packaged_task<RecType()>>(std::bind(std::forward<Func>func,std::forward<Args>(args)...));
        std::future<RecType>result = task->get_future();
         if(m_queue_.put([task](){(*task)();})!=0){
            (*task)();
         }
         if(m_idThreadSize <= 0 && m_curThreadSize < m_maxThreadSize){
            std::lock_guard<std::mutex>loker(mutex_);
            auto tha = std::make_shared<std::thread>(std::thread(&CacheThreadPool::RunInThread,this));
            std::thread::id tid = tha->get_id();
            m_threadgroup.emplace(tid,std::move(tha));
            m_idThreadSize++;
            m_curThreadSize++;
         }
         return result;
    }
};
#endif