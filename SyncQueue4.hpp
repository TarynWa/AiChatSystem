#ifndef _SYNC_TIMER_QUEUE_HPP_
#define _SYNC_TIMER_QUEUE_HPP_

#ifdef __linux__
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <cerrno>
#endif

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <chrono>
#include <utility>
#include <cstdint>

using TaskFunc = std::function<void()>;
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Ms = std::chrono::milliseconds;

// 定时任务结构体
struct TimerTask
{
    uint64_t delay_ms;
    TaskFunc task;
    TimePoint due_time;

    explicit TimerTask(uint64_t delay, TaskFunc&& func)
        : delay_ms(delay), task(std::move(func)), due_time(Clock::now() + Ms(delay))
    {}

    TimerTask() : delay_ms(0), task(nullptr), due_time(Clock::now()) {}

    TimerTask(const TimerTask&) = default;
    TimerTask& operator=(const TimerTask&) = default;
    TimerTask(TimerTask&&) noexcept = default;
    TimerTask& operator=(TimerTask&&) noexcept = default;
};

// 小根堆比较：到期越早优先级越高
struct TimerCmp
{
    bool operator()(const TimerTask& lhs, const TimerTask& rhs) const noexcept
    {
        return lhs.due_time > rhs.due_time;
    }
};

class SyncTimerQueue
{
private:
    // 双队列：延迟堆 + 就绪执行队列
    std::priority_queue<TimerTask, std::vector<TimerTask>, TimerCmp> wait_delay_q_;
    std::queue<TimerTask> ready_run_q_;

    const size_t max_cap_;
    const size_t enqueue_wait_ms_;

    mutable std::mutex mtx_;
    std::condition_variable cv_full_;
    std::condition_variable cv_empty_;
    std::atomic<bool> running_{true};

#ifdef __linux__
    int epoll_fd_ = -1;
    int timer_fd_ = -1;
    int wake_efd_ = -1;
    std::thread schedule_th_;
#endif

    bool IsFull() const
    {
        return wait_delay_q_.size() + ready_run_q_.size() >= max_cap_;
    }

    bool IsAllEmpty() const
    {
        return wait_delay_q_.empty() && ready_run_q_.empty();
    }

#ifdef __linux__
    // 更新timerfd为最近到期任务
    bool ResetTimerFd()
    {
        if (wait_delay_q_.empty())
        {
            itimerspec zero{};
            return timerfd_settime(timer_fd_, 0, &zero, nullptr) == 0;
        }
        auto now = Clock::now();
        const auto& top_task = wait_delay_q_.top();
        itimerspec spec{};
        if (top_task.due_time <= now)
        {
            spec.it_value.tv_nsec = 1;
        }
        else
        {
            auto diff = std::chrono::duration_cast<Ms>(top_task.due_time - now).count();
            spec.it_value.tv_sec = diff / 1000;
            spec.it_value.tv_nsec = (diff % 1000) * 1000000;
        }
        return timerfd_settime(timer_fd_, 0, &spec, nullptr) == 0;
    }

    // 唤醒调度线程
    void WakeScheduler()
    {
        uint64_t sig = 1;
        (void)write(wake_efd_, &sig, sizeof(sig));
    }

    // 调度主循环 epoll 事件驱动
    void ScheduleLoop()
    {
        constexpr int MAX_EV = 2;
        epoll_event evs[MAX_EV];
        while (true)
        {
            std::unique_lock<std::mutex> lock(mtx_);
            if (!running_ && IsAllEmpty())
                break;
            if (!ready_run_q_.empty())
                cv_empty_.notify_one();
            if (!running_ && ready_run_q_.empty())
                break;
            ResetTimerFd();
            lock.unlock();

            // 500ms兜底超时，防止卡死
            int n = epoll_wait(epoll_fd_, evs, MAX_EV, 500);
            if (n <= 0)
                continue;

            std::unique_lock<std::mutex> elock(mtx_);
            for (int i = 0; i < n; ++i)
            {
                int fd = evs[i].data.fd;
                if (fd == timer_fd_)
                {
                    // 定时器到期，迁移所有过期任务
                    uint64_t cnt = 0;
                    (void)read(timer_fd_, &cnt, sizeof(cnt));
                    auto now = Clock::now();
                    while (!wait_delay_q_.empty() && wait_delay_q_.top().due_time <= now)
                    {
                        TimerTask move_t = std::move(const_cast<TimerTask&>(wait_delay_q_.top()));
                        wait_delay_q_.pop();
                        ready_run_q_.push(std::move(move_t));
                    }
                    cv_empty_.notify_all();
                }
                else if (fd == wake_efd_)
                {
                    uint64_t val = 0;
                    (void)read(wake_efd_, &val, sizeof(val));
                }
            }
        }
    }
#endif

    // 底层入队封装
    int InnerEnqueue(TimerTask&& task)
    {
        std::unique_lock<std::mutex> lock(mtx_);
        bool ok = cv_full_.wait_for(lock, Ms(enqueue_wait_ms_),
            [this](){ return !IsFull() || !running_; });
        if (!ok) return 1;
        if (!running_) return 2;

        wait_delay_q_.push(std::move(task));
        lock.unlock();
#ifdef __linux__
        WakeScheduler();
#endif
        cv_empty_.notify_one();
        return 0;
    }

public:
    SyncTimerQueue(size_t max_cap, size_t enqueue_ms = 1000)
        : max_cap_(max_cap), enqueue_wait_ms_(enqueue_ms)
    {
#ifdef __linux__
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        wake_efd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

        if (epoll_fd_ >= 0 && timer_fd_ >= 0 && wake_efd_ >= 0)
        {
            epoll_event ev{};
            ev.events = EPOLLIN;
            ev.data.fd = timer_fd_;
            epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer_fd_, &ev);
            ev.data.fd = wake_efd_;
            epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wake_efd_, &ev);
            schedule_th_ = std::thread(&SyncTimerQueue::ScheduleLoop, this);
        }
#endif
    }

    ~SyncTimerQueue()
    {
        Stop();
#ifdef __linux__
        if (schedule_th_.joinable())
            schedule_th_.join();
        if (epoll_fd_ >= 0) close(epoll_fd_);
        if (timer_fd_ >= 0) close(timer_fd_);
        if (wake_efd_ >= 0) close(wake_efd_);
#endif
    }

    // 停止队列，唤醒所有阻塞线程
    void Stop()
    {
        {
            std::unique_lock<std::mutex> lock(mtx_);
            running_ = false;
        }
#ifdef __linux__
        WakeScheduler();
#endif
        cv_full_.notify_all();
        cv_empty_.notify_all();
    }

    // 对外提交任务接口
    int PutDelayTask(const TimerTask& task)
    {
        return InnerEnqueue(TimerTask(task));
    }

    int PutDelayTask(TimerTask&& task)
    {
        return InnerEnqueue(std::move(task));
    }

    int PutDelayTask(uint64_t delay_ms, TaskFunc&& func)
    {
        return PutDelayTask(TimerTask(delay_ms, std::move(func)));
    }

    // 阻塞获取就绪任务
    int TakeTask(TimerTask& out)
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_empty_.wait(lock, [this](){ return !ready_run_q_.empty() || !running_; });
        if (!ready_run_q_.empty())
        {
            TimerTask tmp = std::move(const_cast<TimerTask&>(ready_run_q_.front()));
            ready_run_q_.pop();
            out = std::move(tmp);
            cv_full_.notify_one();
            return 0;
        }
        return 2;
    }

    SyncTimerQueue(const SyncTimerQueue&) = delete;
    SyncTimerQueue& operator=(const SyncTimerQueue&) = delete;
    SyncTimerQueue(SyncTimerQueue&&) = delete;
    SyncTimerQueue& operator=(SyncTimerQueue&&) = delete;
};

#endif
