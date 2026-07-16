# 线程池与同步队列注释文档

本文件对当前仓库中的线程池实现和同步队列实现进行逐个说明，覆盖类职责、主要成员、工作流程和使用说明。

---

## 1. SyncQueue<T>

### 文件
- `SyncQueue.hpp`

### 作用
- 一个固定容量的线程安全阻塞队列。
- 支持生产者等待队列非满后入队，消费者等待队列非空后出队。
- 提供停止机制，让等待中的线程尽快退出。

### 主要成员
- `std::queue<T> queue_`
  - 存储队列元素。
- `mutable std::mutex mutex_`
  - 保护队列及关联状态。
- `std::condition_variable m_not_empty_`
  - 等待队列非空。
- `std::condition_variable m_not_full_`
  - 等待队列未满。
- `size_t max_size_`
  - 队列最大容量。
- `size_t size_`
  - 当前队列元素数量。
- `std::atomic<bool> needstop_`
  - 停止标志。

### 关键方法
- `SyncQueue(size_t max_size)`
  - 构造函数，初始化容量与状态。
- `~SyncQueue()`
  - 析构时若未停止则调用 `stop()`。
- `void put(const T& x)` / `void put(T&& x)`
  - 入队接口，可能阻塞直到队列可写或停止。
- `void take(T& x)`
  - 出队接口，消费者等待直到队列非空或停止。
- `void take(std::queue<T>& q)`
  - 一次性移动整个内部队列到用户提供的 `q`。
- `bool empty() const` / `bool full() const`
  - 查询当前队列是否为空或已满。
- `void stop()`
  - 标记停止并唤醒所有等待线程。

### 内部实现细节
- `bool isFull() const` / `bool isEmpty() const`
  - 使用 `mutex_` 保护读取 `size_`。
- `template <typename F> void add(F&& x)`
  - 内部入队函数，使用条件变量等待队列可写。

### 使用说明
- 适用于固定容量生产者-消费者模型。
- Destructor 会尝试停止队列，避免线程阻塞泄漏。

---

## 2. FixedThreadPool

### 文件
- `FixedThreadPool.hpp`

### 作用
- 固定线程数量的线程池。
- 提交任务后由工作线程从 `SyncQueue<Task>` 中消费并执行。

### 主要成员
- `SyncQueue<Task> task_queue_`
  - 任务队列。
- `std::vector<std::shared_ptr<std::thread>> threads_`
  - 工作线程容器。
- `std::atomic<bool> is_running_`
  - 池是否正在运行。
- `std::once_flag flag_`
  - 确保 `stop()` 仅执行一次。

### 关键方法
- 构造函数 `FixedThreadPool(int numsize, int max_task_size)`
  - 默认线程数为 `std::thread::hardware_concurrency()`。
  - 初始化任务队列并启动工作线程。
- `void addTask(Task&& task)` / `void addTask(const Task& task)`
  - 将任务提交到队列中。
- `void runworkthread()`
  - 工作线程循环调用 `task_queue_.take(task)`，再执行 `task()`。
- `void stopworker()`
  - 停止运行标志，通知任务队列停止，并 `join` 所有线程。
- `void stop()`
  - 通过 `std::call_once` 保证只停止一次。

### 工作流程
1. 构造时创建固定数量线程并进入 `runworkthread()` 循环。
2. 任务提交到 `task_queue_`。
3. 线程从队列中取任务执行。
4. 调用 `stop()` 时，线程池停止并加入所有线程。

### 使用说明
- 适合任务量长期稳定、线程数量固定的场景。
- `addTask` 发生阻塞时，调用者会等待直到队列可写。

---

## 3. SyncQueue1<T>

### 文件
- `SyncQueue1.hpp`

### 作用
- 一个带超时等待机制的线程安全阻塞队列。
- 与 `SyncQueue` 不同，提交任务时会等待指定时间，如果超时则返回状态码。

### 主要成员
- `std::list<T> queue_`
  - 存储队列元素。
- `std::condition_variable m_not_empty_`
  - 等待队列非空。
- `std::condition_variable m_not_full_`
  - 等待队列未满。
- `size_t max_size_`
  - 队列最大容量。
- `size_t size_`
  - 当前元素数量。
- `size_t m_waittime_`
  - 最长等待秒数。
- `std::atomic<bool> needstop_`
  - 停止标志。

### 关键方法
- `int add(F&& x)`
  - 内部添加函数，等待最多 `m_waittime_` 秒。
  - 返回 `0` 表示成功，`1` 表示等待超时，`2` 表示已停止。
- `void put(const T& x)` / `void put(T&& x)`
  - 调用内部 `add()`。
- `void take(T& x)` / `void take(std::queue<T>& q)`
  - 消费接口，阻塞等待元素到来或停止。
- `bool empty() const` / `bool full() const`
  - 查询队列状态。
- `void stop()`
  - 通知停止并唤醒所有等待线程。
- `size_t size()`
  - 返回当前队列元素数。

### 使用说明
- 可用于需要超时判断的生产者逻辑。
- 仅有 `put()` 返回值可用来判断是否入队成功。

---

## 4. SyncQueue2<T>

### 文件
- `SyncQueue2.hpp`

### 作用
- 一个基于“桶”划分的线程安全队列实现。
- 支持多个 bucket，每个 bucket 内部队列单独存储任务。
- 为每个任务或消费操作接受 `bucketIndex` 参数。

### 主要成员
- `std::vector<std::list<T>> queue_`
  - bucket 列表，每个 bucket 是一个独立队列。
- `std::condition_variable m_notEmpty_`
  - 通知队列可消费。
- `std::condition_variable m_notFull_`
  - 通知队列可写。
- `size_t maxSize_`
  - 每个 bucket 的容量上限。
- `size_t m_bucketSize_`
  - bucket 数量。
- `bool m_isRunning_`
  - 运行标志。

### 关键方法
- `SyncQueue2(const size_t maxSize, const size_t bucketSize)`
  - 构造时创建多个 bucket，并设置可运行状态。
- `int Put(const T& task, const int bucketIndex)` / `int Put(T&& task, const int bucketIndex)`
  - 向指定 bucket 入队。
  - 使用 1 秒超时等待，返回状态码。
- `int Take(T& task, const int bucketIndex)`
  - 从指定 bucket 出队一个元素。
- `int Take(std::list<T>& task, const int bucketIndex)`
  - 从指定 bucket 取出一个元素并赋值给列表对象。
- `void Stop()`
  - 关闭队列，唤醒所有等待者。
- `size_t Size(const int bucketIndex) const`
  - 查询指定 bucket 的大小。

### 行为说明
- `addtask()` 与 `Take()` 都使用 `wait_for` 1 秒超时机制。
- 当 `m_isRunning_` 变为 `false` 时，队列停止并唤醒等待线程。

### 使用说明
- 适合基于不同 `bucketIndex` 进行分发与消费的场景。
- 由于全局 `mutex_`，不同 bucket 仍然存在锁竞争。

---

## 5. WorkThreadPool

### 文件
- `WorkThreadPool.hpp`

### 作用
- 一个带 `submit()` 接口的线程池，使用 `SyncQueue2<task>` 来存储任务。
- 任务提交可返回 `std::future` 以便等待结果。

### 主要成员
- `size_t m_numThreads_`
  - 线程数量。
- `SyncQueue2<task> m_taskQueue_`
  - 任务队列。
- `std::vector<std::shared_ptr<std::thread>> m_threads_`
  - 工作线程容器。
- `std::atomic<bool> m_isRunning_`
  - 线程池运行标志。
- `std::once_flag m_onceFlag_`
  - 保证 `Stop()` 只执行一次。

### 关键方法
- `WorkThreadPool(const size_t numThreads, const size_t maxTaskSize, const size_t bucketSize)`
  - 构造函数，初始化线程池并启动指定数量线程。
- `template<typename F, typename... Args> auto submit(F&& f, Args&&... args)`
  - 提交一个可调用对象，返回对应 `std::future`。
  - 任务包装入 `std::packaged_task`，然后以 lambda 的形式传递给队列。
  - 若 `m_taskQueue_.Put(..., threadIndex()) != 0` 时，直接在当前线程执行任务。
- `void RunInThread(int threadIndex)`
  - 工作线程循环，从对应 bucket 取列表任务并执行。
  - 若当前 bucket 无任务，则尝试从另一个 bucket 获取任务。
- `void StopThreadGroup()`
  - 停止队列并 `join` 所有线程。
- `void Stop()`
  - 通过 `std::call_once` 调用停止流程。

### 工作流程
1. 工作线程循环调用 `m_taskQueue_.Take(tasks, threadIndex)`。
2. 如果当前 bucket 有任务，则批量执行；否则尝试从其他 bucket 获取任务。
3. `submit()` 将任务加入 bucket 决定的队列，或在当前线程执行备用任务。

### 使用说明
- 适合需要通过 `future` 获取任务结果的场景。
- `bucketSize` 可用于按线程索引分配任务。

---

## 6. CacheThreadPool

### 文件
- `CacheThreadPool.hpp`

### 作用
- 类似 `java.util.concurrent.ThreadPoolExecutor` 的缓存线程池设计。
- 维护核心线程数、最大线程数和空闲线程回收机制。

### 主要成员
- `SyncQueue1<task> m_queue_`
  - 任务队列，带超时等待特性。
- `std::unordered_map<std::thread::id, std::shared_ptr<std::thread>> m_threadgroup`
  - 线程对象集合。
- `int m_coreThreadSize`
  - 核心线程数。
- `int m_maxThreadSize`
  - 最大线程数。
- `std::atomic_int m_idThreadSize`
  - 空闲线程数。
- `std::atomic_int m_curThreadSize`
  - 当前线程总数。
- `std::atomic_bool m_running`
  - 线程池运行标志。
- `std::once_flag m_flag`
  - 确保停止一次性执行。

### 关键方法
- `CacheThreadPool(int initNumThreads, int taskPoolsize)`
  - 构造函数，创建核心线程并初始化队列。
- `void execute(Func&& func, Args&&... args)`
  - 提交任务，不返回 `future`。
  - 当队列已满时，会直接在当前线程中执行该任务。
- `auto submit(Func&& func, Args&&... args)`
  - 提交任务并返回 `std::future`。
  - 若空闲线程数不足且当前线程数小于最大线程数，会创建新线程。
- `void RunInThread()`
  - 工作线程函数：从队列中获取任务并执行，同时根据空闲时间和线程池状态回收多余线程。
- `void StopThreadGroup()`
  - 停止队列，关闭线程并清理线程组。
- `void Stop()`
  - 仅执行一次的停止接口。

### 行为说明
- 空闲线程在 `KeepAliveTime` 秒后可被回收，前提是当前线程数超过核心线程数。
- 线程回收使用 `detach()` 并从 `m_threadgroup` 中移除。

### 使用说明
- 适用于任务提交频率不均、需要动态扩缩容的场景。
- `submit()` 返回 `future`，适合需要获取结果或等待任务完成的异步调用。

---

## 7. 备注与注意点

- 代码中存在一些返回值与条件判断的实现细节，使用时需谨慎验证。
- `SyncQueue::isFull()` 和 `SyncQueue::isEmpty()` 在某些分支中未显式返回 `false`，但当前上下文通常不会触发未定义行为。
- `SyncQueue2::addtask()` 和 `Take()` 中的 `wait_for` 逻辑应确保超时判断返回值合理。
- `CacheThreadPool::submit()` 中 `decltype(func(arg...))` 可能存在拼写错误，实际类型推断应使用 `decltype(func(args...))`。

---

## 8. 推荐使用方式

- 若仅需固定数量工作线程：使用 `FixedThreadPool`。
- 若希望任务返回结果：使用 `WorkThreadPool`。
- 若需要动态线程扩缩容：使用 `CacheThreadPool`。
- 若希望简单同步队列：使用 `SyncQueue`。

---

## 9. 术语说明

- `Task` / `task`
  - 当前仓库中多数线程池使用的 `std::function<void()>` 类型，表示一个无参无返回值任务。
- `bucket`
  - `SyncQueue2` 中的分区队列，用于按不同索引存储任务。
- `future`
  - 异步结果对象，`WorkThreadPool` 和 `CacheThreadPool` 提交函数可返回。
