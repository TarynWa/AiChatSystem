# 线程池与同步队列注释文档

本文件对当前仓库中的线程池实现和同步队列实现进行逐个说明，覆盖类职责、主要成员、工作流程、关键行为和使用建议。

---

## 1. SyncQueue<T>

### 文件
- `SyncQueue.hpp`

### 作用
- 固定容量的线程安全阻塞队列。
- 生产者等待队列非满后入队，消费者等待队列非空后出队。
- 支持停止信号，唤醒等待线程并结束阻塞。

### 主要成员
- `std::queue<T> queue_`
  - 存储队列元素。
- `mutable std::mutex mutex_`
  - 保护队列及状态读写。
- `std::condition_variable m_not_empty_`
  - 等待队列非空。
- `std::condition_variable m_not_full_`
  - 等待队列未满。
- `size_t max_size_`
  - 最大容量。
- `size_t size_`
  - 当前元素数量。
- `std::atomic<bool> needstop_`
  - 停止标志。

### 关键方法
- `SyncQueue(size_t max_size)`
  - 初始化容量、大小和停止标志。
- `~SyncQueue()`
  - 如果尚未停止，则调用 `stop()`。
- `void put(const T& x)` / `void put(T&& x)`
  - 阻塞式入队，等待队列可写或停止。
- `void take(T& x)`
  - 阻塞式出队，等待元素或停止。
- `void take(std::queue<T>& q)`
  - 将内部队列整体移动到用户提供的 `q`。
- `bool empty() const` / `bool full() const`
  - 查询状态。
- `void stop()`
  - 设置停止标志并唤醒所有等待线程。

### 实现要点
- `add(F&& x)` 在 `m_not_full_` 条件变量上等待 `!isFull() || needstop_`。
- `take()` 在 `m_not_empty_` 上等待 `!isEmpty() || needstop_`。
- `stop()` 仅设置 `needstop_` 并通知所有等待者。

### 使用场景
- 适合生产者-消费者模型，任务量稳定、队列容量固定。
- `put()` 和 `take()` 都可能阻塞。

---

## 2. SyncQueue1<T>

### 文件
- `SyncQueue1.hpp`

### 作用
- 带超时等待的线程安全阻塞队列。
- 入队时最多等待指定秒数，超时后返回失败状态。

### 主要成员
- `std::list<T> queue_`
  - 存储任务元素。
- `mutable std::mutex mutex_`
  - 保护队列和状态。
- `std::condition_variable m_not_empty_`
  - 等待队列非空。
- `std::condition_variable m_not_full_`
  - 等待队列未满。
- `size_t max_size_`
  - 最大容量。
- `size_t size_`
  - 当前元素数量。
- `size_t m_waittime_`
  - 最长等待秒数。
- `std::atomic<bool> needstop_`
  - 停止标志。

### 关键方法
- `SyncQueue1(size_t max_size)`
  - 初始化队列容量和状态。
- `void put(const T& x)` / `void put(T&& x)`
  - 调用内部 `add()` ，等待最多 `m_waittime_` 秒。
- `void take(T& x)`
  - 阻塞取单个元素，遇到停止则直接返回。
- `void take(std::queue<T>& q)`
  - 将内部队列移动给用户提供的 `q`。
- `bool empty() const` / `bool full() const`
  - 查询当前状态。
- `void stop()`
  - 标记停止并唤醒所有等待者。
- `size_t size()`
  - 返回当前队列元素数。

### 实现要点
- `add()` 使用 `wait_for(..., [this]{ return needstop_ || !isFull(); })`。
- 队列停止后，`put` 与 `take` 不再继续阻塞。
- 析构函数会自动调用 `stop()`。

### 使用场景
- 适用于生产者希望在入队失败时获取超时反馈的场景。
- 适合任务爆发时需要“放弃或降级”策略的系统。

---

## 3. SyncQueue2<T>

### 文件
- `SyncQueue2.hpp`

### 作用
- 基于 bucket 的多队列实现。
- 支持按 `bucketIndex` 将任务分发到不同队列。

### 主要成员
- `std::vector<std::list<T>> queue_`
  - 每个 bucket 的独立队列。
- `mutable std::mutex mutex_`
  - 保护所有 bucket 和条件变量。
- `std::condition_variable m_notEmpty_`
  - 通知消费线程。
- `std::condition_variable m_notFull_`
  - 通知生产线程。
- `size_t maxSize_`
  - 每个 bucket 的容量上限。
- `size_t m_bucketSize_`
  - bucket 数量。
- `bool m_isRunning_`
  - 运行标志。

### 关键方法
- `SyncQueue2(const size_t maxSize, const size_t bucketSize)`
  - 构造函数，初始化 bucket 数量和运行状态。
- `int Put(const T& task, const int bucketIndex)`
  - 向指定 bucket 入队，使用 1 秒超时等待。
- `int Put(T&& task, const int bucketIndex)`
  - 向指定 bucket 移动入队。
- `int Take(T& task, const int bucketIndex)`
  - 从指定 bucket 读取一个元素。
- `int Take(std::list<T>& task, const int bucketIndex)`
  - 从指定 bucket 读取一个对象列表（当前实现有语义问题，见注意点）。
- `void Stop()`
  - 关闭队列并唤醒所有等待线程。
- `size_t Size(const int bucketIndex) const`
  - 查询指定 bucket 的当前大小。

### 实现要点
- 所有 bucket 共享同一个 `mutex_`，因此仍然会发生锁竞争。
- `Put` 和 `Take` 使用超时等待，返回值需要按照当前实现判定。

### 使用场景
- 适用于需要按任务类别/线程索引分发的场景。
- 若希望减少跨 bucket 的竞争，应改造为多锁设计。

---

## 4. SyncQueue3

### 文件
- `SyncQueue3.hpp`

### 作用
- 优先级队列 + 延迟调度的任务队列。
- 支持在 `PatrTask.first` 上进行延迟控制。

### 主要成员
- `std::priority_queue<PatrTask, std::vector<PatrTask>, std::greater<PatrTask>> tasks`
  - 按 `first` 值排序的任务优先队列。
- `size_t max_size`
  - 最大容量。
- `mutable std::mutex mtx`
  - 保护队列和状态。
- `std::condition_variable m_not_full`
  - 通知生产者。
- `std::condition_variable m_not_empty`
  - 通知消费者。
- `size_t waittime_`
  - 生产者等待超时毫秒数。
- `bool is_running_`
  - 队列运行状态。

### 关键方法
- `int Put(const PatrTask& task)`
  - 向优先队列入队，等待 `waittime_` 毫秒。
  - 返回 `0` 表示成功，`1` 表示超时，`2` 表示队列已停止。
- `int Take(const PatrTask& task)`
  - 读取队列头任务并弹出。
  - 当前实现中，`task` 应为可写引用，但接口为 `const PatrTask&`。
- `void Stop()`
  - 将 `is_running_` 设为 `false`，等待队列清空后唤醒所有等待线程。

### 实现要点
- 任务入队后，`m_not_empty_` 唤醒一个消费者。
- `Take()` 在取出任务后，使用 `m_not_full.wait_for()` 延迟通知生产者。
- `Stop()` 会等待队列清空后再通知所有线程。

### 使用场景
- 适用于基于优先级和延迟时间调度任务的线程池。

---

## 5. FixedThreadPool

### 文件
- `FixedThreadPool.hpp`

### 作用
- 固定线程池实现。
- 使用 `SyncQueue<Task>` 存储任务并由线程消费。

### 主要成员
- `SyncQueue<Task> task_queue_`
- `std::vector<std::shared_ptr<std::thread>> threads_`
- `std::atomic<bool> is_running_`
- `std::once_flag flag_`

### 关键方法
- `FixedThreadPool(int numsize = std::thread::hardware_concurrency(), int max_task_size = 200)`
  - 构造函数，创建固定数量线程并启动。
- `void addTask(Task&& task)` / `void addTask(const Task& task)`
  - 提交新任务。
- `void runworkthread()`
  - 线程循环取任务并执行。
- `void stopworker()`
  - 停止运行并 `join` 所有线程。
- `void stop()`
  - 仅执行一次的停止接口。

### 工作流程
1. 构造时立即创建工作线程。
2. 任务通过 `addTask()` 入队。
3. 线程在 `runworkthread()` 中循环取出任务并执行。
4. 调用 `stop()` 后，线程池退出并回收资源。

### 使用场景
- 适合任务量稳定、并发度可预知的场景。
- 不适用于需要动态伸缩的任务流。

---

## 6. WorkThreadPool

### 文件
- `WorkThreadPool.hpp`

### 作用
- 基于 `SyncQueue2<task>` 的线程池。
- 支持按 bucket 分发任务，并返回 `std::future`。

### 主要成员
- `size_t m_numThreads_`
- `SyncQueue2<task> m_taskQueue_`
- `std::vector<std::shared_ptr<std::thread>> m_threads_`
- `std::atomic<bool> m_isRunning_`
- `std::once_flag m_onceFlag_`

### 关键方法
- `WorkThreadPool(const size_t numThreads, const size_t maxTaskSize, const size_t bucketSize)`
  - 初始化任务队列并启动线程。
- `auto submit(F&& f, Args&&... args)`
  - 提交任务并返回 `future`。
  - 若 `Put()` 失败，则在当前线程直接执行该任务。
- `void RunInThread(int threadIndex)`
  - 工作线程根据 bucket 读取任务列表并执行。
- `void StopThreadGroup()`
  - 停止队列并回收线程。

### 工作流程
1. 每个线程以自身索引为主要消费 bucket。
2. `submit()` 根据 `threadindex()` 选择写入 bucket。
3. 线程优先从当前 bucket 取任务，若无任务则尝试其他 bucket。
4. `Stop()` 停止所有工作线程。

### 使用场景
- 适合需要 `future` 结果的异步任务提交。
- 可用于按索引分区调度任务。

---

## 7. ScheduleThreadPool

### 文件
- `ScheduleThreadPool.hpp`

### 作用
- 基于调度队列的线程池实现。
- 使用 `SyncQueue3` 存储带延迟/优先级的任务。

### 主要成员
- `SyncQueue3 task_queue`
- `std::list<std::shared_ptr<std::thread>> threads`
- `std::atomic<bool> is_running_`
- `std::once_flag once_flag`

### 关键方法
- `ScheduleThreadPool(int numthreads, int max_size, int waittime_)`
  - 构造函数，初始化队列和线程列表。
- `void Start(int numthreads)`
  - 启动工作线程。
- `void WorkerThread()`
  - 从队列取任务并执行。
- `void StopGroupThread()`
  - 停止队列并等待线程退出。
- `auto Submit(F&& f, Args&&... args)`
  - 提交带 `future` 的任务。

### 工作流程
1. `Start()` 创建线程并进入 `WorkerThread()` 循环。
2. 线程从 `SyncQueue3` 获取任务。
3. `Submit()` 将任务封装为 `packaged_task` 并尝试入队。
4. 若 `Put()` 返回失败，则直接在当前线程执行任务。

### 使用场景
- 适合需要按延迟或优先级调度任务的场景。
- 可用于需要异步结果的调度执行。

---

## 8. CacheThreadPool

### 文件
- `CacheThreadPool.hpp`

### 作用
- 类似缓存线程池结构。
- 动态扩缩容线程池，保持核心线程数并回收空闲线程。

### 主要成员
- `SyncQueue1<task> m_queue_`
- `std::unordered_map<std::thread::id,std::shared_ptr<std::thread>> m_threadgroup`
- `int m_coreThreadSize`
- `int m_maxThreadSize`
- `std::atomic_int m_idThreadSize`
- `std::atomic_int m_curThreadSize`
- `std::atomic_bool m_running`
- `std::once_flag m_flag`

### 关键方法
- `CacheThreadPool(int initNumThreads = 8, int taskPoolsize = MaxTaskCount)`
  - 初始化核心线程数和任务队列。
- `void execute(Func&& func, Args&&... args)`
  - 非 future 提交接口，队列满时在当前线程执行。
- `auto submit(Func&& func, Args&&... args)`
  - 提交任务并返回 `future`。
  - 在空闲线程不足时可创建新线程。
- `void RunInThread()`
  - 工作线程线程函数：从队列取任务并执行。
  - 若空闲时间超过 `KeepAliveTime` 且线程数高于核心线程数，则回收线程。
- `void StopThreadGroup()`
  - 停止队列，关闭并回收线程。

### 工作流程
1. 构造函数创建核心线程组。
2. 任务提交到 `SyncQueue1`。
3. 空闲线程在队列空闲期间检测超时，可能自我回收。
4. `Stop()` 关闭线程池并回收所有线程。

### 使用场景
- 适合负载峰谷明显、希望线程池自动伸缩的场景。
- `submit()` 适合需要异步结果的调用。

---

## 9. 重要注意事项

- `SyncQueue` 的 `isFull()` / `isEmpty()` 缺少显式 `return false;`，应修正为完整返回值。
- `SyncQueue1` 的 `wait_for` 语法及超时返回值需要调整。
- `SyncQueue2` 的 `wait_for` 返回值约定与常见用法不一致，当前实现中 `true` 表示提前唤醒或超时成功，需要谨慎判断。
- `SyncQueue3::Take()` 目前使用 `const PatrTask& task` 作为输出参数，但实现对该参数赋值，存在接口错误。
- `WorkThreadPool::submit()` 中 `threadIndex()` 和 `threadindex()` 名称不一致，可能导致编译错误。
- `CacheThreadPool::submit()` 中 `decltype(func(arg...))` 代码存在拼写问题。
- `ScheduleThreadPool` 的 `Start()` / `Stop()` 共享一个 `once_flag`，启动后不能再次安全调用 `Stop()`。

---

## 10. 推荐使用方式

- 固定线程数：`FixedThreadPool`
- 异步结果：`WorkThreadPool`、`ScheduleThreadPool`
- 动态扩缩容：`CacheThreadPool`
- 简单阻塞队列：`SyncQueue`
- 带超时入队：`SyncQueue1`
- 按 bucket 分发：`SyncQueue2`
- 延迟/优先级调度：`SyncQueue3` + `ScheduleThreadPool`

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
