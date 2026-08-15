# chatsystem 压测报告

> 测试时间：2026-08-14
> 压测工具：[tools/chat_stress.py](file:///home/wangt/ThreadPoolAction/tools/chat_stress.py)（自研，纯 protobuf 手工编解码）
> 服务端：[src/chatsystem/chatserver.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatserver.cpp) + [src/chatsystem/chatservice.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp)

---

## 一、测试环境

### 1.1 硬件 / 系统

| 项目 | 配置 |
|------|------|
| CPU | AMD Ryzen 7 6800H，16 核 |
| 内存 | 7.4 GB（测试时已用 2.8 GB） |
| OS | Linux（WSL2） |
| 磁盘 | NVMe SSD |

### 1.2 服务端部署架构

```
                ┌─────────────┐
   压测客户端 ──→│  Nginx TCP  │── 8080 端口（stream 模块负载均衡）
                │  负载均衡   │
                └──┬──────┬───┘
                   │      │
            ┌──────┘      └──────┐
            ▼                     ▼
   ┌─────────────────┐   ┌─────────────────┐
   │  chatserver #1   │   │  chatserver #2   │
   │  127.0.0.1:6000  │   │  127.0.0.1:6001  │
   │  muduo Reactor   │   │  muduo Reactor   │
   │  ThreadPool=4    │   │  ThreadPool=4    │
   │  MySQL Pool=10   │   │  MySQL Pool=10   │
   └─────────────────┘   └─────────────────┘
            │                     │
            ▼                     ▼
   ┌─────────────────────────────────────┐
   │  MySQL 8.x（chat 库）              │
   │  Redis（在线用户 SET + 跨节点 Pub/Sub）│
   └─────────────────────────────────────┘
```

### 1.3 关键服务端配置

| 组件 | 配置 | 代码位置 |
|------|------|----------|
| muduo 网络线程 | 默认（1 主 loop + sub loops） | [chatserver.cpp#L48](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatserver.cpp#L48)（`setThreadNum` 被注释） |
| 业务线程池 | 4 个工作线程 | [chatservice.hpp#L59](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.hpp#L59)（`threadpool_.start(4)`） |
| MySQL 连接池 | 10 个连接 | [mysql.hpp#L59](file:///home/wangt/ThreadPoolAction/src/chatsystem/mysql.hpp#L59)（`maxSize=10`） |
| Redis | 单实例，SET + Pub/Sub | [RedisMgr.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/RedisMgr.cpp) |
| Nginx | TCP stream 负载均衡，2 后端 | 端口 8080 → 6000/6001 |

### 1.4 压测方法

- **阶梯加压**：并发数从 16 逐级翻倍（16→32→64→128→256），每并发用户执行 N 次操作
- **同步模式**：发一条请求 → 等待 ACK → 发下一条（与服务端 `retrieveAllAsString()` 单帧解析语义匹配）
- **指标**：RPS（每秒成功请求数）、成功率、P50/P95/P99 延迟
- **提前停止条件**：成功率 < 70% 或连续 2 级 RPS 无增长

---

## 二、压测结果

### 2.1 单聊（stress_one_chat）

**场景**：每个 worker 登录后向固定目标用户发送 N 条私聊消息（纯消息投递，不含建连成本）

| 并发数 | 每用户次数 | 耗时 | 成功率 | RPS | P50 | P95 | P99 |
|--------|-----------|------|--------|-----|-----|-----|-----|
| 16 | 20 | 0.90s | 99.7% (311/312) | **345** | 33.6ms | 57.7ms | 75.5ms |
| 32 | 20 | 2.08s | 99.8% (621/622) | 299 | 81.7ms | 101.9ms | 117.1ms |
| 64 | 20 | 3.84s | 99.9% (1271/1272) | 331 | 162.4ms | 193.1ms | 210.4ms |

- **峰值 RPS**：345（16 并发）
- **拐点**：≈16 并发（超过后 RPS 不再增长，延迟线性上升）
- **错误**：3 次 `varint 截断`（客户端 socket 分包解析偶发失败）

### 2.2 群聊（stress_group_chat）

**场景**：16 人群组，每个 worker 登录后向群发送 N 条消息（服务端异步查成员 + 本地转发 + Redis 跨节点 + 离线存储）

| 并发数 | 每用户次数 | 耗时 | 成功率 | RPS | P50 | P95 | P99 |
|--------|-----------|------|--------|-----|-----|-----|-----|
| 16 | 10 | 0.43s | 14.6% (18/123) | 41 | 11.8ms | 72.4ms | 95.6ms |

- **峰值 RPS**：41（16 并发，提前停止）
- **错误**：7 次 `varint 截断`；大量 ACK 未被正确解析

### 2.3 加好友（stress_add_friend）

**场景**：每个 worker 登录后向不同有效用户发送加好友请求（异步 DB 双向 INSERT）

| 并发数 | 每用户次数 | 耗时 | 成功率 | RPS | P50 | P95 | P99 |
|--------|-----------|------|--------|-----|-----|-----|-----|
| 16 | 5 | 0.47s | 100.0% (80/80) | **168** | 55.1ms | 78.7ms | 83.0ms |
| 32 | 5 | 0.67s | 49.4% (79/160) | 117 | 55.5ms | 94.9ms | 99.6ms |

- **峰值 RPS**：168（16 并发）
- **拐点**：≈16 并发（32 并发时成功率暴跌至 49.4%）
- **失败原因**：`FriendModel::insert` 双向插入 `(A,B)+(B,A)`，高并发时不同 worker 的反向插入命中唯一键冲突

### 2.4 删好友（stress_del_friend）

**场景**：每个 worker 登录后向固定目标用户发送删好友请求（异步 DB 双向 DELETE）

| 并发数 | 每用户次数 | 耗时 | 成功率 | RPS | P50 | P95 | P99 |
|--------|-----------|------|--------|-----|-----|-----|-----|
| 16 | 5 | 0.33s | 100.0% (80/80) | 243 | 12.6ms | 66.2ms | 70.8ms |
| 32 | 5 | 0.60s | 100.0% (160/160) | 267 | 53.3ms | 72.2ms | 84.7ms |
| 64 | 5 | 1.17s | 100.0% (320/320) | **274** | 65.1ms | 136.7ms | 157.1ms |

- **峰值 RPS**：274（64 并发）
- **拐点**：≈64 并发（连续 2 级 RPS 无增长，提前停止）
- **全程 100% 成功率**，DELETE 幂等无冲突

### 2.5 总览对比

| 业务 | 峰值 RPS | 拐点并发 | 16并发成功率 | P95@16 | 核心瓶颈 |
|------|---------|---------|-------------|--------|---------|
| 单聊 | **345** | 16 | 99.7% | 57.7ms | muduo IO 线程单 loop |
| 删好友 | **274** | 64 | 100% | 66.2ms | ThreadPool(4) + DB Pool(10) |
| 加好友 | **168** | 16 | 100% | 78.7ms | 双向 INSERT 唯一键冲突 |
| 群聊 | **41** | <16 | 14.6% | 72.4ms | 广播放大 + 客户端 recv 解析 |

---

## 三、优点分析

### 3.1 异步架构设计合理
- 所有 DB 操作通过 `threadpool_.run()` 派发到 4 个工作线程，不阻塞 muduo IO 线程
- 单聊、删好友等"轻广播"业务在 16 并发下延迟可控（P95 < 80ms）
- 参见 [chatservice.cpp#L332](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L332)（addFriend 异步派发）

### 3.2 连接池复用有效
- MySQL 连接池（10 连接）在 64 并发删好友时仍保持 100% 成功率
- 连接获取/释放通过 `shared_ptr` 自定义 deleter 自动管理，无泄漏
- 参见 [mysql.hpp#L37](file:///home/wangt/ThreadPoolAction/src/chatsystem/mysql.hpp#L37)

### 3.3 DELETE 操作表现优异
- 删好友在 16/32/64 三个并发级别全部 100% 成功
- DELETE 幂等性强，即使目标不是好友也返回 code=0，无冲突
- 峰值 274 RPS，是加好友的 1.6 倍

### 3.4 单聊消息投递高效
- 在线用户命中本地 `_userConnMap` 时直接转发，无 DB 开销
- 345 RPS 的峰值在 4 线程 ThreadPool 配置下表现合理
- 跨节点通过 Redis Pub/Sub，离线存 MySQL，三级路由清晰

### 3.5 集群可扩展
- Nginx TCP 负载均衡 + Redis 全局在线 SET，支持多节点水平扩展
- 跨节点消息通过 Redis `chat:cross_node` 频道转发

---

## 四、缺点与瓶颈分析

### 4.1 ❗ 协议无消息分帧（致命缺陷）

**现象**：群聊和加好友使用批量发送（Fire-and-forget）时成功率极低（10~28%），服务端日志显示大量 `Deserialization failed!`

**根因**：[chatserver.cpp#L19](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatserver.cpp#L19)
```cpp
string buff = buffer->retrieveAllAsString();  // 取出整个缓冲区
chatservice::instance()->recvmsg(conn, buff, time);  // 当作 1 条 protobuf 解析
```
服务端将 TCP 缓冲区全部内容当作**一条** protobuf 消息解析。当客户端连续发送多条消息时，TCP 合包（Nagle / 内核合并）导致多条消息被 `retrieveAllAsString()` 一次读出，`ParseFromString()` 解析失败，**整批消息被丢弃**。

**影响**：无法支持高频批量发送；压测脚本被迫改为同步模式（发一条等一条），实际吞吐被 RTT 拖低。

**修复建议**：
- 方案 A（推荐）：在 `BaseMessage` 外层加 4 字节长度前缀（`len + payload`），服务端 `onMessage` 按长度切分循环解析
- 方案 B：muduo `Buffer` 使用 `retrieveAll` 后手动按 protobuf 字段边界切分（复杂且脆弱）

### 4.2 ❗ 群聊广播放大效应

**现象**：16 人群聊在 16 并发下成功率仅 14.6%

**根因**：[chatservice.cpp#L541](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.cpp#L541) groupChat 对每条消息：
1. 异步查群成员（1 次 DB SELECT）
2. 遍历 16 成员，查本地 `_userConnMap`
3. 未命中成员逐个查 Redis `SISMEMBER`
4. 离线成员逐个 INSERT 离线消息

16 个 worker × 10 条消息 = 160 条群聊，每条广播给 15 个成员 = **2400 次 send**。客户端 socket 收到大量广播包，与 ACK 粘包，`try_split_multi` 解析失败。

**影响**：群聊实际可用并发 < 16；客户端无法有效解析混合了广播消息和 ACK 的粘包流。

### 4.3 ❗ 加好友双向 INSERT 冲突

**现象**：32 并发时成功率从 100% 暴跌至 49.4%

**根因**：[FriendModel.cpp#L10-L11](file:///home/wangt/ThreadPoolAction/src/chatsystem/FriendModel.cpp#L10)
```cpp
string sql1 = "INSERT INTO Friend(userid, friendid) VALUES(" + userid + ", " + friendid + ")";
string sql2 = "INSERT INTO Friend(userid, friendid) VALUES(" + friendid + ", " + userid + ")";
```
好友关系双向插入 `(A,B)+(B,A)`。当 worker A 加 B、worker B 加 A 并发执行时，`(B,A)` 和 `(A,B)` 命中唯一键冲突，事务回滚，返回 code=2。

**影响**：高并发加好友场景成功率不可控。

**修复建议**：
- 使用 `INSERT IGNORE` 或 `INSERT ... ON DUPLICATE KEY UPDATE` 容忍冲突
- 或先 `SELECT` 检查是否已为好友，避免无效插入

### 4.4 ⚠️ ThreadPool 仅 4 线程

**现象**：单聊拐点在 16 并发，RPS 不随并发增长

**根因**：[chatservice.hpp#L59](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatservice.hpp#L59) `threadpool_.start(4)` 仅启动 4 个工作线程。所有异步 DB 任务（注册、加好友、删好友、群聊查成员、离线消息存储）共享这 4 个线程。16 并发时任务队列积压，延迟线性上升。

**影响**：单聊 64 并发 P99 达 210ms（16 并发的 2.8 倍），但 RPS 反而下降。

### 4.5 ⚠️ muduo setThreadNum 被注释

**现象**：[chatserver.cpp#L48](file:///home/wangt/ThreadPoolAction/src/chatsystem/chatserver.cpp#L48) `// server_.setThreadNum(4);` 被注释，muduo 使用默认线程数。网络 IO 事件处理可能集中在少数 sub-loop，限制单节点连接处理能力。

### 4.6 ⚠️ DB 操作未使用预编译语句

**现象**：[FriendModel.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/FriendModel.cpp)、[UserModel.cpp](file:///home/wangt/ThreadPoolAction/src/chatsystem/UserModel.cpp) 等使用字符串拼接 SQL，每次调用 `mysql_query` 都需解析 SQL。在高频删除/插入场景增加 DB CPU 开销。

### 4.7 ⚠️ 客户端 recv 解析脆弱

**现象**：压测脚本 `recv_all` 仅读 2 次 recv（5ms 间隔），`try_split_multi` 用启发式切分 protobuf 帧群聊广播场景下大量粘包解析失败（`varint 截断`）。

---

## 五、性能拐点总结

```
RPS
 400 │  ★ 单聊 345
     │   ╲
 300 │    ╲──╮           ★ 删好友 274
     │       ╲  ╲       ╱──╮
 200 │        ╲  ╲     ╱   ╲──
     │         ╲  ╲   ╱
 100 │   ★加友168╲ ╱
     │          ╲╱──────(暴跌)
  50 │  ★群聊41
     └───┬────┬────┬────┬────┬──
        16   32   64   128  256
                 并发数
```

- **单聊**：16 并发达峰值，延迟随并发线性增长
- **删好友**：64 并发达峰值，全程 100% 成功
- **加好友**：16 并发达峰值，32 并发因双向冲突暴跌
- **群聊**：16 并发即失败，广播放大 + 协议缺陷

---

## 六、优化建议

### 优先级 P0（致命问题）

| # | 问题 | 建议 | 预期收益 |
|---|------|------|---------|
| 1 | 协议无分帧 | BaseMessage 加 4 字节长度前缀，onMessage 循环切分 | 支持批量发送，吞吐翻倍 |
| 2 | 加好友双向冲突 | `INSERT IGNORE` 替代 `INSERT` | 32 并发成功率恢复至 95%+ |

### 优先级 P1（性能瓶颈）

| # | 问题 | 建议 | 预期收益 |
|---|------|------|---------|
| 3 | ThreadPool=4 | 调至 8~16（匹配 MySQL Pool=10） | 单聊拐点从 16 提至 32+ |
| 4 | muduo setThreadNum 注释 | 取消注释，设为 4 | 提升网络 IO 并行度 |
| 5 | 群聊广播放大 | 成员分类后批量 PUBLISH；客户端单独线程收广播 | 群聊成功率提升 |

### 优先级 P2（工程质量）

| # | 问题 | 建议 |
|---|------|------|
| 6 | SQL 字符串拼接 | 改用 `mysql_stmt_prepare` 预编译语句，防注入 + 提速 |
| 7 | 离线消息逐条 INSERT | 改为批量 INSERT（`INSERT INTO ... VALUES (),(),()`） |
| 8 | Redis 逐个 SISMEMBER | 用 `SMEMBERS` 一次取回在线集合，本地 Set 查找 |

---

## 七、结论

| 维度 | 评价 |
|------|------|
| 架构设计 | ★★★★☆ Reactor + ThreadPool + 连接池 + Redis 集群，分层清晰 |
| 单聊性能 | ★★★★☆ 345 RPS，99.7% 成功率，延迟可控 |
| 删好友性能 | ★★★★★ 274 RPS，64 并发 100% 成功 |
| 加好友性能 | ★★★☆☆ 168 RPS@16 并发，高并发冲突严重 |
| 群聊性能 | ★★☆☆☆ 41 RPS，14.6% 成功率，广播放大 + 协议缺陷 |
| 协议健壮性 | ★★☆☆☆ 无长度分帧，粘包导致整批丢弃 |
| 可扩展性 | ★★★★☆ Nginx + Redis 支持水平扩展 |

**总体评价**：系统在低并发（≤16）单聊/删好友场景表现优秀，异步架构有效隔离了 DB 阻塞。但协议层缺少消息分帧是致命缺陷，直接导致群聊和高频批量场景不可用。加好友的双向 INSERT 冲突是可快速修复的工程问题。建议优先修复 P0 级协议分帧和 INSERT 冲突，可显著提升整体可用并发量。
