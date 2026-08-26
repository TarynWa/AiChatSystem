#ifndef FIXED_THREAD_POOL_HPP
#define FIXED_THREAD_POOL_HPP
#include "SyncQueue.hpp"
#include <thread>
#include <functional>
#include <atomic>
#include <vector>
#include <memory>
using Task = std::function<void()>;
class FixedThreadPool {
    private:
        SyncQueue<Task> task_queue_;
        std::vector<std::shared_ptr<std::thread>> threads_;
        std::atomic<bool> is_running_;
        std::once_flag flag_;
        void StartWorker(int numsize){
            is_running_ = true;
            for(size_t i = 0 ; i < numsize; ++i){
                threads_.emplace_back(std::make_shared<std::thread>(&FixedThreadPool::runworkthread, this));
            }
        }
        void runworkthread(){
            while(is_running_){
                Task task;
                task_queue_.take(task);
                if(!is_running_){
                    return;
                }
                task();
            }
        }
        void stopworker(){
            is_running_ = false;
            task_queue_.stop();
            for(auto& thread : threads_){
                if(thread->joinable()){
                    thread->join();
                }
            }
        }
        public:
        FixedThreadPool(int numsize = std::thread::hardware_concurrency(), int max_task_size = 200) : task_queue_(max_task_size), is_running_(false) {
            StartWorker(numsize);
        }
        ~FixedThreadPool(){
            stop();
        }
        void addTask(Task&& task){
            task_queue_.put(std::forward<Task>(task));
        }
        void addTask(const Task& task){
            task_queue_.put(task);
        }
        void stop(){
            std::call_once(flag_, [this](){stopworker();});
        }
};

#endif // FIXED_THREAD_POOL_HPP