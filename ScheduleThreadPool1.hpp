#ifndef _SCHEDULE_THREAD_POOL_HPP_
#define _SCHEDULE_THREAD_POOL_HPP_

#include "SyncQueue4.hpp"
#include <atomic>
#include <vector>
#include <thread>
#include <functional>

using TaskFunc = std::function<void()>;

class ScheduleThreadPool
{
public:
    /// @brief 构造线程池
    /// @param worker_num 工作线程数量
    /// @param max_task 队列最大容纳任务总数
    /// @param enqueue_ms 入队等待超时(ms)
    explicit ScheduleThreadPool(size_t worker_num, size_t max_task, size_t enqueue_ms = 1000)
        : queue_(max_task, enqueue_ms), running_(true)
    {
        workers_.reserve(worker_num);
        for (size_t i = 0; i < worker_num; ++i)
        {
            workers_.emplace_back(&ScheduleThreadPool::WorkerLoop, this);
        }
    }

    // 禁止拷贝移动
    ScheduleThreadPool(const ScheduleThreadPool&) = delete;
    ScheduleThreadPool& operator=(const ScheduleThreadPool&) = delete;
    ScheduleThreadPool(ScheduleThreadPool&&) = delete;
    ScheduleThreadPool& operator=(ScheduleThreadPool&&) = delete;

    ~ScheduleThreadPool()
    {
        Stop();
    }

    /// 提交延迟任务
    /// @param delay_ms 延迟毫秒
    /// @param func 待执行函数
    /// @return 0成功 1入队超时 2队列已停止
    int SubmitDelayTask(uint64_t delay_ms, TaskFunc&& func)
    {
        return queue_.PutDelayTask(delay_ms, std::move(func));
    }

    /// 停止线程池与定时队列
    void Stop()
    {
        if (!running_.exchange(false))
            return;
        queue_.Stop();
        // 回收所有工作线程
        for (auto& th : workers_)
        {
            if (th.joinable())
                th.join();
        }
    }

private:
    // 工作线程循环：阻塞从队列取就绪任务执行
    void WorkerLoop()
    {
        TimerTask task;
        while (running_)
        {
            int ret = queue_.TakeTask(task);
            if (ret != 0)
                break;
            // 执行业务任务
            if (task.task)
                task.task();
        }
    }

private:
    SyncTimerQueue queue_;
    std::atomic<bool> running_;
    std::vector<std::thread> workers_;
};

#endif
