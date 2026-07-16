#ifndef SyncQueue2_hpp
#define SyncQueue2_hpp
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

template <typename T> class SyncQueue2 {
private:
  std::vector<std::list<T>> queue_;
  mutable std::mutex mutex_;
  std::condition_variable m_notEmpty_;
  std::condition_variable m_notFull_;
  size_t maxSize_;
  size_t m_bucketSize_;
  bool m_isRunning_;
  bool IsFull(const int bucketIndex) const {
    return queue_[bucketIndex].size() >= maxSize_;
  }
  bool IsEmpty(const int bucketIndex) const {
    return queue_[bucketIndex].empty();
  }
  template <typename F> int addtask(F &&task, const int bucketIndex) {
    std::unique_lock<std::mutex> lock(mutex_);
    bool waitret =
        m_notFull_.wait_for(lock, std::chrono::seconds(1), [this, bucketIndex] {
          return !IsFull(bucketIndex) || !m_isRunning_;
        });
    if (waitret) {
      return 1;
    }
    if (!m_isRunning_) {
      return 2;
    }
    queue_[bucketIndex].push_back(std::forward<F>(task));
    m_notEmpty_.notify_one();
    return 0;
  }
  public:
  SyncQueue2(const size_t maxSize, const size_t bucketSize) : maxSize_(maxSize), m_bucketSize_(bucketSize), m_isRunning_(true) {
    queue_.resize(bucketSize);
  }
  ~SyncQueue2() {
    
  }
  int Put(const T& task, const int bucketIndex) {
    return addtask(task, bucketIndex);
  }
  int Put(T&& task, const int bucketIndex) {
    return addtask(std::move(task), bucketIndex);
  }
  int Take(T& task, const int bucketIndex) {
    std::unique_lock<std::mutex> lock(mutex_);
    bool waitret = m_notEmpty_.wait_for(lock, std::chrono::seconds(1), [this, bucketIndex]{return !IsEmpty(bucketIndex)||!m_isRunning_;});
    if(waitret){
      return 1;
    }
    if(!m_isRunning_){
      return 2;
    }
    task = queue_[bucketIndex].front();
    queue_[bucketIndex].pop_front();
    m_notFull_.notify_one();
    return 0;
  }
  int Take(std::list<T>& task, const int bucketIndex) {
    std::unique_lock<std::mutex> lock(mutex_);
    bool waitret = m_notEmpty_.wait_for(lock, std::chrono::seconds(1), [this, bucketIndex]{return !IsEmpty(bucketIndex)||!m_isRunning_;});
    if(waitret){
      return 1;
    }
    if(!m_isRunning_){
      return 2;
    }
    task = std::move(queue_[bucketIndex].front());
    queue_[bucketIndex].pop_front();
    m_notFull_.notify_one();
    return 0;
  }
  void Stop(){
    std::unique_lock<std::mutex> lock(mutex_);
    m_isRunning_ = false;
    for(int i = 0; i < m_bucketSize_; i++){
      m_notFull_.wait(lock);
  }
  m_notEmpty_.notify_all();
  m_notFull_.notify_all();
  }
  size_t Size(const int bucketIndex) const {
    return queue_[bucketIndex].size();
  }
};
#endif // SyncQueue2_hpp