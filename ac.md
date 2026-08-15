# ThreadPoolAction 项目面试考点详解

> 本文档梳理项目面试中可能考察的核心知识点，每条均带源码位置链接（IDE 内可点击跳转），方便复习与回答时定位。
> 项目结构：C++ muduo 聊天服务（主） + Rust/Tauri 桌面客户端 + Python AI 客服。

---

## 目录

1. [项目整体架构与分层](#1-项目整体架构与分层)
2. [线程池模块（threadpool/）](#2-线程池模块threadpool)
3. [异步日志模块（logSystem/）](#3-异步日志模块logsystem)
4. [muduo Reactor 网络模型](#4-muduo-reactor-网络模型)
5. [Protobuf 协议设计](#5-protobuf-协议设计)
6. [业务服务层 chatservice](#6-业务服务层-chatservice)
7. [数据库设计与异步封装](#7-数据库设计与异步封装)
8. [密码安全（SHA256 + 随机盐）](#8-密码安全sha256--随机盐)
9. [集群架构与 Redis 跨节点路由](#9-集群架构与-redis-跨节点路由)
10. [Nginx TCP 负载均衡](#10-nginx-tcp-负载均衡)
11. [Tauri 桌面客户端](#11-tauri-桌面客户端)
12. [AI 智能客服（src/chat_ai）](#12-ai-智能客服srcchat_ai)
13. [经典面试问答预演](#13-经典面试问答预演)
14. [可能被追问的"坑"与改进点](#14-可能被追问的坑与改进点)

---

## 1. 项目整体架构与分层

**问法**：介绍一下这个项目的整体架构？数据流是怎么走的？

**回答框架**：

- **分层架构**：
  ```
  前端 UI（HTML/JS）  ←→  Tauri IPC  ←→  Rust std::net TCP  ←→  C++ muduo 服务
                                                    ↕
                                       Nginx stream 负载均衡 (8080)
                                                    ↕
                                  chatservice 节点1 (6000) / 节点2 (6001)
                                       ↕                  ↕
                                  MySQL（业务数据）  Redis（在线用户 + Pub/Sub 跨节点）
  ```

- **技术栈**：
  - C++17 / muduo 网络库 / protobuf / MySQL / hiredis / OpenSSL
  - Python FastAPI + Ollama + ChromaDB（AI 客服）
  - Rust + Tauri 2.x（桌面客户端）
  - Nginx stream 模块（4 层负载均衡）

- **数据流举例（发送私聊）**：
  1. 前端 JS 调用 `Tauri.invoke('send_private', {fromId, toId, content})`
  2. Rust 后台 `send_msg(ONE_CHAT_MSG=6, payload)`
  3. TCP 流到 nginx:8080 → 转发到 6000 或 6001
  4. muduo `onMessage` → `chatservice::recvmsg` 解析 `BaseMessage` → 分发到 `oneChat` handler
  5. handler 查 `_userConnMap`：本节点在线 → 直接 `conn->send`；其他节点在线 → Redis `PUBLISH chat:cross_node`；离线 → MySQL `OfflineMessage` 表
  6. 对端客户端读线程收到 protobuf → emit Tauri 事件 `chat.one_chat` → 前端渲染

**源码定位**：
- 服务端入口：[TestLogThreadPool.cpp:20-46](file:///home/wangt/ThreadPoolAction/tests/TestLogThreadPool.cpp#L20-L46)
- 消息分发：[chatservice.cpp:9-25](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L9-L25)
- 客户端 TCP 层：[tcp.rs](file:///home/wangt/ThreadPoolAction/chat-client/src-tauri/src/tcp.rs)

---

## 2. 线程池模块（threadpool/）

**问法**：你的线程池是怎么实现的？为什么要自己写而不用 C++ 标准库？

**核心要点**：

1. **任务队列**：`SyncQueue<T>` 是带容量上限的生产者-消费者队列，内部 `std::list<task>` + `mutex` + `condition_variable`
   - `Put()`：队列满时阻塞或返回错误码
   - `Take()`：队列空时阻塞，被唤醒后批量取任务（减少锁竞争）
   - `Stop()`：唤醒所有等待线程让其退出

2. **worker 线程模型**：
   ```cpp
   void RunInThread(int threadIndex) {
       while (m_isRunning_) {
           std::list<task> tasks;
           if (m_taskQueue_.Take(tasks, threadIndex) == 0) {
               for (auto& task : tasks) task();
           }
       }
   }
   ```
   - 批量取任务：一次 `Take` 拿多条，避免每条任务都抢锁
   - 线程亲和性尝试：先尝试取自己 bucket 的任务，没有再窃取其他 bucket

3. **submit() 接口**：
   ```cpp
   template<typename F, typename... Args>
   auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))>;
   ```
   - 用 `std::packaged_task` 包装任务，返回 `std::future` 让调用方可同步等待
   - 完美转发 `std::forward<F>(f)` 保留可调用对象的左右值属性
   - 队列满时直接在当前线程执行（降级策略，避免阻塞 IO 线程）

4. **多种实现版本对比**：
   - `SyncQueue.hpp` / `SyncQueue1-4.hpp`：从基础到进化的多个版本（可用于讲解演进过程）
   - `FixedThreadPool.hpp`：固定线程数
   - `CacheThreadPool.hpp`：可动态扩缩容
   - `WorkThreadPool.hpp`：项目实际使用版本，带 bucket 分片
   - `ScheduleThreadPool.hpp`：支持定时/周期任务

**追问点**：
- **为什么不用标准库？** 标准库无线程池（仅有 `std::thread`/`std::async`）；`std::async` 默认策略不保证立即执行；自实现可控制队列容量、批量取、降级策略
- **队列满怎么处理？** 降级到调用方线程同步执行（见 [WorkThreadPool.hpp:71-73](file:///home/wangt/ThreadPoolAction/threadpool/WorkThreadPool.hpp#L71-L73)）
- **如何优雅停止？** `std::call_once` + `Stop()` 设置 `m_isRunning_=false` → 队列 `Stop()` 唤醒所有等待 → `join()` 所有线程

**源码定位**：
- 核心实现：[WorkThreadPool.hpp](file:///home/wangt/ThreadPoolAction/threadpool/WorkThreadPool.hpp)
- 同步队列：[SyncQueue2.hpp](file:///home/wangt/ThreadPoolAction/threadpool/SyncQueue2.hpp)

---

## 3. 异步日志模块（logSystem/）

**问法**：日志系统是怎么设计的？为什么要异步？

**核心要点**：

1. **双缓冲机制**（muduo 风格）：
   - `currentBuffer_`：前端线程 append 日志到这里
   - `buffers_`：已写满待刷盘的缓冲队列
   - 后台 flush 线程定时（`flushInterval_`）或缓冲满时把 `buffers_` 写到文件

2. **append 流程**（[AsyncLogging.cpp:44-59](file:///home/wangt/ThreadPoolAction/logSystem/logsys/src/AsyncLogging.cpp#L44-L59)）：
   ```cpp
   void append(const char* info, int len) {
       std::unique_lock<std::mutex> _lock(mutex_);
       if (currentBuffer_.size() >= BufMaxLen) {
           buffers_.push_back(std::move(currentBuffer_));  // 满了丢到待刷队列
           currentBuffer_.clear();
           currentBuffer_.reserve(BufMaxLen);
       } else {
           currentBuffer_.append(info, len);
       }
       cond_.notify_all();
   }
   ```
   - 前端只持有锁很短时间（拷贝到缓冲区），不会阻塞业务线程
   - `notify_all` 唤醒后台 flush 线程

3. **日志滚动 LogFile**：
   - 按 `rollSize` 滚动（文件大小超阈值则新建）
   - 按时间滚动（每天一个文件）
   - 文件名格式：`server.YYYYMMDDHHMMSS.<pid>.HOST.log`

4. **为什么异步？**
   - 同步写盘每次 IO 耗时 ms 级，会阻塞网络 IO 线程
   - 异步后端批量刷盘，前端只拷贝内存，纳秒级开销
   - 即使日志磁盘满了，业务依然能继续处理请求

**追问点**：
- **日志丢失怎么办？** 进程崩溃时 `currentBuffer_` 中未刷盘的日志会丢；可通过析构时 `flush()` 兜底（[AsyncLogging.cpp:18-25](file:///home/wangt/ThreadPoolAction/logSystem/logsys/src/AsyncLogging.cpp#L18-L25)）
- **多线程同时写日志会乱序吗？** `mutex_` 保证 `append` 串行；后台 flush 单线程串行写文件，顺序由入队时间决定
- **如果日志量很大，队列满了怎么办？** 当前实现 `buffers_` 是 `vector` 无上限，OOM 风险存在；可加有界队列 + 丢弃策略（改进点）

**源码定位**：
- 异步日志：[AsyncLogging.cpp](file:///home/wangt/ThreadPoolAction/logSystem/logsys/src/AsyncLogging.cpp)
- 日志文件：[LogFile.cpp](file:///home/wangt/ThreadPoolAction/logSystem/logsys/src/LogFile.cpp)
- 日志接口：[Logger.cpp](file:///home/wangt/ThreadPoolAction/logSystem/logsys/src/Logger.cpp)

---

## 4. muduo Reactor 网络模型

**问法**：为什么用 muduo？Reactor 模式怎么工作的？

**核心要点**：

1. **muduo 的 "one loop per thread" 模型**：
   - 主线程 `EventLoop` 跑 `epoll_wait`，监听 listen fd
   - 新连接到来时，按 round-robin 分配到 sub Reactor（sub EventLoop）的 epoll
   - 每条连接的所有读写都在固定的 sub EventLoop 中完成，**无锁**

2. **关键组件**：
   - `EventLoop`：事件循环（封装 epoll）
   - `Channel`：fd + 关心事件的可观测对象
   - `TcpServer`：管理监听 + 连接生命周期
   - `TcpConnection`：单条 TCP 连接的抽象，带 input/output buffer
   - `Buffer`：muduo 自带的 buffer，应用层流量控制

3. **本项目的使用**（[chatserver.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatserver.cpp)）：
   ```cpp
   void ChatServer::onMessage(const TcpConnectionPtr& conn, Buffer* buffer, Timestamp time) {
       string buff = buffer->retrieveAllAsString();   // 取出全部数据
       chatservice::instance()->recvmsg(conn, buff, time);
   }
   ```
   - `onConnection`：连接建立/断开回调
   - `onMessage`：数据到达回调
   - 注意：当前实现用 `retrieveAllAsString` 一次取完，**没有按长度分帧**——这是后面压测/兼容性要注意的点

4. **为什么 muduo 而不是手写 epoll？**
   - muduo 已封装好跨平台、Buffer、定时器、连接管理
   - "Reactor + 线程池" 经典模式，易扩展
   - 社区成熟，文档清晰

**追问点**：
- **muduo 是 LT 还是 ET？** 默认 LT（水平触发），减少误读 EOF 风险，但需配合非阻塞 IO
- **为什么不在 IO 线程做 DB 操作？** 阻塞 IO 线程会拖累所有连接；本项目把 DB 操作派发到 `threadpool_` 异步执行，结果通过 `runInLoop` 投递回 IO 线程 send
- **连接断开怎么感知？** `onConnection` 回调里 `conn->connected()` 返回 false 时，调用 `clientCloseException` 清理 `_userConnMap`

**源码定位**：
- 服务器：[chatserver.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatserver.cpp)
- 服务器头：[chatserver.hpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatserver.hpp)

---

## 5. Protobuf 协议设计

**问法**：客户端和服务端怎么通信？为什么用 protobuf？

**核心要点**：

1. **协议结构**（[chat.proto](file:///home/wangt/ThreadPoolAction/src/chatsystem/chat.proto)）：
   ```proto
   enum EnMsgType {
       MSG_NONE = 0;          // proto3 首项必须为 0
       LOGIN_MSG = 1;
       LOGIN_MSG_ACK = 2;
       REG_MSG = 4;
       // ...
       CROSS_NODE_CHAT_MSG = 20;
   }

   message BaseMessage {
       EnMsgType type = 1;    // 业务类型
       bytes payload = 2;     // 内层业务消息序列化字节
   }
   ```
   - **"信封 + 内层 payload"** 设计：外层 BaseMessage 决定如何反序列化 payload
   - 这是一种 **类型擦除** 模式，便于在不重新编译协议的情况下扩展新消息

2. **proto3 规则**：
   - enum 首项必须为 0（用于表示默认/未知）
   - 字段去掉 `required`/`optional`，全部 `singular`（向后兼容友好）
   - 不支持 default 值

3. **帧格式**：
   - 本项目 **不使用长度前缀**，直接 TCP 流发送 `BaseMessage.SerializeToString()`
   - 服务端 `retrieveAllAsString()` 一次性取出缓冲区全部字节直接 `ParseFromString`
   - **隐患**：TCP 粘包会导致问题；目前依赖 muduo 一次性读到完整消息的假设（改进点）

4. **为什么 protobuf 而不是 JSON？**
   - 体积小（二进制 varint 编码，字段名不传输）
   - 解析快（直接内存映射，无需字符串解析）
   - 强类型 + schema 演进友好
   - 缺点：不可读，需 `.proto` 文件才能解码

**追问点**：
- **如何新增一个消息类型？** 在 `EnMsgType` 追加枚举值（不复用历史值），新增 `message XxxRequest {}`，在 `_msgHandlerMap` 注册 handler
- **proto3 与 proto2 区别？** proto3 删 required/optional、字段默认值不能自定义、枚举首项必须 0、原生支持 map
- **字段号有什么规则？** 1-15 用 1 字节 varint 编码（高频字段优先）；16-2047 用 2 字节；19000-19999 保留

**源码定位**：
- 协议定义：[chat.proto](file:///home/wangt/ThreadPoolAction/src/chatsystem/chat.proto)
- 生成的 C++ 代码：[chat.pb.h](file:///home/wangt/ThreadPoolAction/src/chatsystem/chat.pb.h)
- 生成的 Rust 代码：[target/debug/build/chat-client-*/out/chat.rs](file:///home/wangt/ThreadPoolAction/chat-client/src-tauri/target/debug/build/chat-client-494623afd1c28d4e/out/chat.rs)

---

## 6. 业务服务层 chatservice

**问法**：登录流程是怎样的？消息是怎么分发的？

**核心要点**：

1. **单例模式**（[chatservice.cpp:3-7](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L3-L7)）：
   ```cpp
   chatservice* chatservice::instance() {
       static chatservice service;
       return &service;
   }
   ```
   - Meyers 单例，C++11 起线程安全
   - 全局唯一入口，便于在 `onMessage` 中获取

2. **消息分发**（[chatservice.cpp:9-25](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L9-L25)）：
   ```cpp
   void recvmsg(const TcpConnectionPtr& conn, const string& js, Timestamp time) {
       chat::BaseMessage menu;
       if (!menu.ParseFromString(js)) { /* 反序列化失败 */ return; }
       chat::EnMsgType tp = menu.type();
       if (_msgHandlerMap.count(tp)) {
           _msgHandlerMap[tp](conn, menu.payload(), time);
       }
   }
   ```
   - 用 `std::unordered_map<EnMsgType, Handler>` 做 O(1) 分发
   - 在构造函数中 `insert` 注册所有 handler（[chatservice.hpp:60-69](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.hpp#L60-L69)）

3. **登录流程**（[chatservice.cpp:28-124](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L28-L124)）：
   - 解析 `LoginRequest`
   - 同步 `_userModel->query(id)` 取出 User（这里其实应该异步化，改进点）
   - 用 `PwdUtils::verify` 校验密码（SHA256(salt + plaintext) == hashed）
   - 成功后：
     - 更新 User 状态为 "online"
     - 加锁插入 `_userConnMap[id] = conn`
     - `RedisMgr::addUserOnline(id)` 同步到全局在线 SET
     - 派发到线程池拉取并投递离线消息

4. **用户连接管理 `_userConnMap`**：
   - `unordered_map<int, TcpConnectionPtr>` 用 mutex 保护
   - 登录时插入，注销/连接断开时 erase
   - 私聊/群聊时通过它查找目标 conn 直接 `conn->send`

**追问点**：
- **`_userConnMap` 加锁粒度？** 整个 map 一把锁；高并发下可分片（按 uid 取模）减小争用（改进点）
- **登录失败为什么也回 ACK？** 让客户端知道失败原因，不能让前端一直转圈
- **离线消息怎么投递？** 登录成功后异步查 `OfflineMessage` 表，循环 `conn->send`，最后 `remove` 清空（[chatservice.cpp:74-106](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L74-L106)）

**源码定位**：
- 主分发：[chatservice.cpp:9-25](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L9-L25)
- 登录业务：[chatservice.cpp:28-124](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L28-L124)
- handler 注册：[chatservice.hpp:58-70](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.hpp#L58-L70)

---

## 7. 数据库设计与异步封装

**问法**：数据库是怎么设计的？为什么所有 DB 操作都异步？

**核心要点**：

1. **5 张表设计**（可在 `mysql.cpp` 或 `init.sql` 中查看建表语句）：
   - `User`：id / name / pwd / salt / state（online/offline）
   - `Friend`：userid / friendid（联合主键）
   - `Group`：id / groupname / groupdesc
   - `GroupUser`：groupid / userid / role（creator/normal）
   - `OfflineMessage`：userid / fromid / msgtype / content

2. **异步派发模式**（以注册为例，[chatservice.cpp:159-](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L159)）：
   ```cpp
   threadpool_.run([this, conn, loop, name, password]() {
       // 在线程池中执行 DB 查询
       User existingUser = _userModel->queryByName(name);
       // ...
       // 通过 runInLoop 把响应投递回 IO 线程发送
       loop->runInLoop([conn, buf]() { conn->send(buf); });
   });
   ```
   - 关键：**禁止在 IO 线程直接调 mysql_query**，会阻塞所有连接
   - DB 操作完成后，用 `EventLoop::runInLoop` 把 `conn->send` 调度回该 conn 所属的 IO 线程（muduo 的 thread-switch 语义）

3. **MySQL 连接**：
   - `mysql.cpp` 封装连接池或单连接
   - 使用 `MYSQL_PWD` 环境变量传密码，避免 `ps` 看到（[project_memory.md](file:///root/.trae-cn/memory/projects/-home-wangt-ThreadPoolAction--p2-626f87ef0d29330fd6da/project_memory.md) 教训）

4. **Model 层职责**：
   - `UserModel`：用户 CRUD
   - `FriendModel`：好友关系
   - `GroupModel`：群组 + 群成员
   - `OfflineMsgModel`：离线消息存取

**追问点**：
- **DB 连接池实现？** 每个线程一个连接（线程局部）或连接池
- **事务怎么处理？** 好友关系是双向的，需要事务保证两条记录同时成功（改进点）
- **为什么 group_id 不自增？** 用 AUTO_INCREMENT；项目设计已预留集群扩展（可改 Snowflake）

**源码定位**：
- MySQL 封装：[mysql.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/mysql.cpp)
- User 模型：[UserModel.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/UserModel.cpp)
- 离线消息：[OfflineMsgModel.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/OfflineMsgModel.cpp)

---

## 8. 密码安全（SHA256 + 随机盐）

**问法**：密码是怎么存储的？为什么不能用 MD5？

**核心要点**（[PwdUtils.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/PwdUtils.cpp)）：

1. **生成盐**：
   ```cpp
   unsigned char buf[16] = {0};
   RAND_bytes(buf, sizeof(buf));   // OpenSSL CSPRNG，16 字节
   char hex[33];
   for (...) snprintf(hex + i*2, 3, "%02x", buf[i]);  // 转 32 位 hex
   ```
   - 用 `RAND_bytes` 而非 `rand()`，密码学安全随机源
   - 16 字节 = 128 bit 熵，足够防彩虹表

2. **哈希**：
   ```cpp
   SHA256(input.data(), input.size(), hash);
   // 转 64 位 hex 字符串
   ```
   - `SHA256(salt + plainPassword)`，盐与密码拼接后哈希
   - 存储的是 64 字符 hex

3. **校验**（[PwdUtils.cpp:35-43](file:///home/wangt/ThreadPoolAction/src/chatsystem/PwdUtils.cpp#L35-L43)）：
   ```cpp
   bool verify(const string& plainPassword, const string& salt, const string& hashedPassword) {
       string computed = sha256(salt + plainPassword);
       return computed == hashedPassword;
   }
   ```

4. **为什么不用 MD5 / 单 SHA256？**
   - MD5 已破解（碰撞）
   - 无盐的 SHA256 易被彩虹表攻击
   - **更安全的方案**：bcrypt / scrypt / Argon2（带迭代次数，防 GPU 暴力破解）——本项目改进点

**追问点**：
- **盐要存哪里？** 与密码一起存 User 表的 `salt` 字段，盐不需要保密
- **盐为什么每个用户独立？** 防止"撞库"——同一密码不同用户哈希不同
- **为什么用 SHA256 不用 SHA1？** SHA1 已被破解；SHA256 仍安全
- **如何防暴力破解？** 加迭代（如 bcrypt cost=12）；本项目当前单次 SHA256，理论可被字典暴力破解（改进点）

**源码定位**：
- 加密工具：[PwdUtils.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/PwdUtils.cpp) / [PwdUtils.hpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/PwdUtils.hpp)

---

## 9. 集群架构与 Redis 跨节点路由

**问法**：如果一台机器扛不住怎么办？多节点之间怎么协同？

**核心要点**：

1. **Redis 数据结构**：
   - `chat:online_users`：SET，存全局在线用户 id（所有节点共享）
   - `chat:cross_node`：Pub/Sub channel，跨节点消息转发

2. **用户上线流程**（[chatservice.cpp:54](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L54)）：
   ```cpp
   RedisMgr::instance()->addUserOnline(id);  // SADD chat:online_users id
   ```

3. **跨节点消息路由三级决策**（[chatservice.cpp:566-613](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L566-L613)）：
   ```
   发送私聊/群聊消息
        ↓
   1. 查本地 _userConnMap → 命中 → conn->send 直接投递
        ↓ 未命中
   2. 查 Redis SISMEMBER chat:online_users → 命中 → PUBLISH chat:cross_node
        ↓ 未命中
   3. 存入 MySQL OfflineMessage 表
   ```

4. **RedisMgr 设计**（[RedisMgr.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/RedisMgr.cpp)）：
   - 两条独立连接：`_publishCtx` 用于 PUBLISH，`_subscribeCtx` 用于 SUBSCRIBE
   - 原因：hiredis 在 SUBSCRIBE 模式下阻塞等待消息，无法复用同一连接做 PUBLISH
   - SUBSCRIBE 线程独立运行，收到消息后回调到 chatservice

5. **跨节点消息格式**（[chat.proto:142-146](file:///home/wangt/ThreadPoolAction/src/chatsystem/chat.proto#L142-L146)）：
   ```proto
   message CrossNodeMsg {
       int64 target_user_id = 1;   // 接收节点在 _userConnMap 中查找
       int32 msg_type = 2;          // 原始消息类型
       string payload = 3;          // 原始消息序列化字节
   }
   ```

**追问点**：
- **Redis 挂了怎么办？** 当前实现 RedisMgr 失败仅打日志；可降级为单节点运行（仅本节点用户能收消息）
- **用户在节点 A 登录后，连接断了但 Redis 没清理怎么办？** 需要心跳机制：服务端定期 PING，无响应则清理（改进点）
- **为什么用 Redis 而不是 MQ（Kafka/RabbitMQ）？** Redis 已有，Pub/Sub 延迟低；MQ 更可靠但运维复杂
- **Pub/Sub 会丢消息吗？** 会！订阅者不在线时发布的消息直接丢失；所以离线消息必须存 MySQL，不依赖 Pub/Sub 投递
- **如何扩展到 N 个节点？** Redis 在线 SET 天然支持；nginx upstream 加 server 即可

**源码定位**：
- Redis 管理：[RedisMgr.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/RedisMgr.cpp) / [RedisMgr.hpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/RedisMgr.hpp)
- 群聊跨节点逻辑：[chatservice.cpp:595-613](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L595-L613)
- 跨节点消息定义：[chat.proto:142-146](file:///home/wangt/ThreadPoolAction/src/chatsystem/chat.proto#L142-L146)

---

## 10. Nginx TCP 负载均衡

**问法**：多节点怎么对外暴露统一入口？

**核心要点**：

1. **stream 模块**（4 层负载，非 http 7 层）：
   - 监听 8080 端口
   - upstream 后端为 6000 / 6001 两个 chatservice 节点
   - 默认 round-robin 调度

2. **为什么用 4 层而非 7 层？**
   - 通信协议是裸 TCP + protobuf，不是 HTTP
   - 4 层只看 TCP/IP 头，开销低
   - 7 层需要解析 HTTP，本项目用不上

3. **健康检查**：
   - nginx 商业版有主动健康检查
   - 开源版依赖被动检查：连接失败标记为 down，过一段时间重试

4. **会话保持问题**：
   - TCP 长连接：客户端连一次，nginx 维持到某后端的长连接，直到断开
   - **隐患**：用户 A 在节点 1 登录，断线重连后 nginx 可能分配到节点 2，但 _userConnMap 在节点 1——需要通过 Redis 全局在线 SET 解决（已实现）

**追问点**：
- **粘性会话怎么做？** 4 层可用 `ip_hash`（按客户端 IP 哈希）；但 IP 变化（NAT/移动网络）会失效
- **后端节点 down 怎么感知？** nginx 被动检查 + 应用层心跳
- **如何滚动升级？** nginx 标记某节点为 `down` → 等连接自然结束 → 升级 → 上线

---

## 11. Tauri 桌面客户端

**问法**：客户端怎么实现的？为什么用 Tauri 不用 Electron？

**核心要点**：

1. **架构分层**：
   ```
   前端 HTML/CSS/JS (src/)         ←  原生，无框架
        ↕ Tauri IPC (invoke / emit)
   Rust 后端 (src-tauri/src/)      ←  Tauri 2.x
        ↕ std::net::TcpStream
   C++ muduo 服务端
   ```

2. **Tauri 2.x 关键 API**：
   - `#[tauri::command]` 注册可被前端 invoke 的函数
   - `app.emit("event_name", payload)` Rust → 前端推送事件
   - 前端 `window.__TAURI__.core.invoke('login', {...})` 调用 Rust 命令
   - 前端 `window.__TAURI__.event.listen('chat.one_chat', cb)` 监听事件

3. **TCP 通信线程模型**（[tcp.rs](file:///home/wangt/ThreadPoolAction/chat-client/src-tauri/src/tcp.rs)）：
   - `TcpClient` 持有 `Mutex<Option<TcpStream>>` + `AppHandle`
   - `connect()`：主线程发起连接
   - `spawn_reader()`：单独 `std::thread` 循环 `recv`，不阻塞主线程
   - 收到消息后 `app.emit("chat.xxx", payload)` 推送到前端
   - **关键**：用 `try_clone()` 复制 fd 给读线程，主线程也可写

4. **断线重连**（[tcp.rs](file:///home/wangt/ThreadPoolAction/chat-client/src-tauri/src/tcp.rs)）：
   - 读线程 `recv` 返回 0（EOF）→ 设置 `running=false` → emit `net_status: disconnected`
   - 若 `current_user` 存在（已登录过），尝试 `try_reconnect()`，间隔 3 秒
   - 重连成功后用户需重新登录（因 _userConnMap 已失效）

5. **为什么 Tauri 而非 Electron？**
   - 二进制小（Tauri ~10MB，Electron ~100MB+）
   - 内存少（用系统 WebView，不打包 Chromium）
   - Rust 后端性能高，可直接调系统 API

**追问点**：
- **WebView 兼容性？** Windows 用 WebView2，macOS 用 WKWebView，Linux 用 WebKitGTK；不同平台行为有差异
- **如何调试？** 右键 → 检查元素；或 `tauri.conf.json` 设置 `app.withGlobalTauri: true`
- **Tauri 2.x 与 1.x 区别？** 2.x 用 capabilities 替代 allowlist；API 改为 `@tauri-apps/api/core`；移动端支持

**源码定位**：
- 前端入口：[src/index.html](file:///home/wangt/ThreadPoolAction/chat-client/src/index.html)
- 前端逻辑：[src/app.js](file:///home/wangt/ThreadPoolAction/chat-client/src/app.js)
- Rust 命令：[src-tauri/src/lib.rs](file:///home/wangt/ThreadPoolAction/chat-client/src-tauri/src/lib.rs)
- TCP 层：[src-tauri/src/tcp.rs](file:///home/wangt/ThreadPoolAction/chat-client/src-tauri/src/tcp.rs)
- 配置：[src-tauri/tauri.conf.json](file:///home/wangt/ThreadPoolAction/chat-client/src-tauri/tauri.conf.json)

---

## 12. AI 智能客服（src/chat_ai）

**问法**：AI 客服怎么实现的？用了哪些技术？

**核心要点**：

1. **技术栈**：
   - **FastAPI**：异步 Web 框架，路由见 [routers/](file:///home/wangt/ThreadPoolAction/src/chat_ai/routers/chat.py)
   - **Ollama**：本地 LLM 推理服务，模型如 `qwen3.5:9B`
   - **ChromaDB**：向量数据库，存储文档/历史聊天的 embedding
   - **bge-m3**：embedding 模型，把文本转为向量
   - **RAG**：检索增强生成

2. **RAG 流程**：
   ```
   用户提问 → bge-m3 向量化 → ChromaDB 相似度检索 → top-k 文档 → 拼到 prompt → Ollama 生成回答
   ```

3. **会话管理**：
   - SQLite 存会话与消息（[database/messages.py](file:///home/wangt/ThreadPoolAction/src/chat_ai/database/messages.py)）
   - session_service 管理多轮对话上下文

4. **联网搜索**（[services/web_search.py](file:///home/wangt/ThreadPoolAction/src/chat_ai/services/web_search.py)）：
   - 支持 DuckDuckGo（免费）和 Tavily（付费）
   - LLM 预判是否需要搜索（节省 API 调用）

5. **流式输出**：
   - SSE 推送到前端，逐 token 显示
   - [services/stream_engine.py](file:///home/wangt/ThreadPoolAction/src/chat_ai/services/stream_engine.py)

**追问点**：
- **RAG 为什么比纯微调好？** RAG 无需训练，知识更新只需更新文档库；微调成本高、更新慢
- **向量检索用什么距离？** cosine 余弦相似度（默认）
- **如何避免幻觉？** RAG 提供 grounded context；可加 citation 引用源
- **embedding 维度？** bge-m3 输出 1024 维

**源码定位**：
- 主入口：[main.py](file:///home/wangt/ThreadPoolAction/src/chat_ai/main.py)
- RAG 引擎：[services/rag_engine.py](file:///home/wangt/ThreadPoolAction/src/chat_ai/services/rag_engine.py)
- 向量存储：[services/vector_store.py](file:///home/wangt/ThreadPoolAction/src/chat_ai/services/vector_store.py)
- 配置：[.env.example](file:///home/wangt/ThreadPoolAction/src/chat_ai/.env.example)

---

## 13. 经典面试问答预演

### Q1: 介绍下你的项目

**模板回答**（30 秒版）：

> 这是一个 C++ 高并发聊天系统，后端基于 muduo Reactor 模型 + 自研线程池 + 异步日志，业务包括注册/登录/私聊/群聊/好友管理。集群通过 Redis 在线用户 SET + Pub/Sub 实现跨节点消息路由，nginx 做 4 层负载均衡。客户端用 Tauri + Rust + 原生 JS 实现，TCP 直连后端 protobuf 协议。另集成一个基于 FastAPI + Ollama + RAG 的 AI 客服模块。我主要负责了 XX 模块的设计与实现。

### Q2: 你遇到的最难的问题是什么？

**候选回答**：

1. **跨节点消息丢失**：初期用 Redis Pub/Sub 直接转发消息，订阅者不在线时消息丢失。改方案：先查 Redis `SISMEMBER` 判断在线状态，离线则存 MySQL `OfflineMessage` 表，登录时拉取（[chatservice.cpp:595-613](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L595-L613)）

2. **muduo IO 线程阻塞**：初期把 MySQL 查询写在 IO 线程，导致所有连接卡顿。改方案：用 `threadpool_.run()` 派发到工作线程，结果通过 `loop->runInLoop` 回 IO 线程发送（[chatservice.cpp:159](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L159)）

3. **Tauri 客户端登录无响应**：Tauri 2.x 的 `window.__TAURI__` 注入路径与 1.x 不同，前端调用 `invoke` 静默失败。修复：兼容多种 IPC 路径（ESM `@tauri-apps/api/core` → 全局 2.x → 全局 1.x → Mock）

### Q3: 并发量多少？怎么测的？

**回答要点**：

- 写了 Python 压测脚本 [tools/chat_stress.py](file:///home/wangt/ThreadPoolAction/tools/chat_stress.py)（纯 socket + 手工 protobuf 编码，无依赖）
- 阶梯式加压：32 → 64 → 128 → 256 → 512 → 1024 并发
- 测试维度：RPS、P50/P95/P99 延迟、成功率、错误分布
- 拐点判定：连续 2 级 RPS 不增长或成功率 < 70%
- 关注指标：登录建连 QPS、私聊消息 QPS、注册 DB 写入 QPS

### Q4: 怎么保证线程安全？

- `_userConnMap`：`std::mutex` + `lock_guard` 保护
- `RedisMgr`：操作前加 `_mutex`，保证 `_publishCtx` 不被多线程并发使用
- `AsyncLogging`：双缓冲 + `mutex_`，前端只锁很短时间
- muduo 自身："one loop per thread"，每条连接绑定固定 IO 线程，无锁

### Q5: 如果让你重新设计，会怎么改？

1. **协议加长度前缀**：4 字节 big-endian 长度 + protobuf，彻底解决粘包
2. **DB 连接池**：替代当前每线程一连接
3. **密码用 bcrypt**：防 GPU 暴力破解
4. **`_userConnMap` 分片**：按 uid 分 16 桶，减小锁粒度
5. **加心跳**：服务端定期 PING，超时清理连接 + Redis 在线状态
6. **离线消息用 MQ**：Kafka 替代 MySQL 直写，提高吞吐
7. **监控指标**：Prometheus + Grafana 采集 RPS / 延迟 / 错误率
8. **分布式 trace**：Jaeger 跟踪消息跨节点流转

---

## 14. 可能被追问的"坑"与改进点

> 面试官常追问"这个设计有什么问题"，提前准备如下：

### 14.1 协议层

| 问题 | 现状 | 改进 |
|---|---|---|
| TCP 粘包 | 服务端 `retrieveAllAsString` 假设一次收到完整消息 | 加 4 字节长度前缀或 delimiter |
| 协议无版本号 | 新旧版本不兼容 | BaseMessage 加 `version` 字段 |

### 14.2 并发层

| 问题 | 现状 | 改进 |
|---|---|---|
| `_userConnMap` 全局锁 | 高并发下争用 | 分片锁 / `concurrent_hash_map` |
| 线程池无优先级 | 所有任务平等 | 加优先级队列 |
| 日志 buffers_ 无上限 | OOM 风险 | 有界队列 + 丢弃策略 |

### 14.3 集群层

| 问题 | 现状 | 改进 |
|---|---|---|
| Redis 单点 | 挂了跨节点不可用 | Redis Cluster / Sentinel |
| Pub/Sub 丢消息 | 订阅者不在线消息丢 | 已用 MySQL 兜底，但 Pub/Sub 仍可能丢 |
| 无心跳 | 僵尸连接占着 _userConnMap | 加心跳 + 超时清理 |
| 节点亲和性 | 重连后可能换节点 | ip_hash 或会话 token |

### 14.4 安全层

| 问题 | 现状 | 改进 |
|---|---|---|
| 密码哈希 | 单次 SHA256 + 盐 | bcrypt / Argon2 |
| 明文传输 | TCP 无加密 | TLS 或 SSH 隧道 |
| SQL 注入 | 已用预编译？需确认 | 必须 `mysql_real_escape_string` 或 prepared statement |
| 权限控制 | 无 RBAC | 加用户角色 + 接口鉴权 |

### 14.5 可观测性

| 问题 | 现状 | 改进 |
|---|---|---|
| 无 metrics | 仅日志 | Prometheus exporter |
| 无 trace | 跨节点难追踪 | OpenTelemetry |
| 无告警 | 故障靠人发现 | Alertmanager + 钉钉/邮件 |

---

## 附：核心源码索引

| 模块 | 文件 |
|---|---|
| 线程池 | [WorkThreadPool.hpp](file:///home/wangt/ThreadPoolAction/threadpool/WorkThreadPool.hpp) |
| 同步队列 | [SyncQueue2.hpp](file:///home/wangt/ThreadPoolAction/threadpool/SyncQueue2.hpp) |
| 异步日志 | [AsyncLogging.cpp](file:///home/wangt/ThreadPoolAction/logSystem/logsys/src/AsyncLogging.cpp) |
| ChatServer | [chatserver.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatserver.cpp) |
| ChatService | [chatservice.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp) / [chatservice.hpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.hpp) |
| Protobuf 协议 | [chat.proto](file:///home/wangt/ThreadPoolAction/src/chatsystem/chat.proto) |
| 密码安全 | [PwdUtils.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/PwdUtils.cpp) |
| Redis 管理 | [RedisMgr.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/RedisMgr.cpp) |
| MySQL | [mysql.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/mysql.cpp) |
| User 模型 | [UserModel.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/UserModel.cpp) |
| 离线消息 | [OfflineMsgModel.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/OfflineMsgModel.cpp) |
| 客户端 TCP | [tcp.rs](file:///home/wangt/ThreadPoolAction/chat-client/src-tauri/src/tcp.rs) |
| 客户端命令 | [lib.rs](file:///home/wangt/ThreadPoolAction/chat-client/src-tauri/src/lib.rs) |
| 客户端前端 | [app.js](file:///home/wangt/ThreadPoolAction/chat-client/src/app.js) |
| AI 客服 | [main.py](file:///home/wangt/ThreadPoolAction/src/chat_ai/main.py) |
| 压测脚本 | [chat_stress.py](file:///home/wangt/ThreadPoolAction/tools/chat_stress.py) |
