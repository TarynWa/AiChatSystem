# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目描述

基于 **muduo Reactor 模型** 的 Linux 集群聊天服务器（C++17），项目起源于线程池学习仓库（ThreadPoolAction），后演进为完整的集群聊天系统：

- 网络层：muduo（EPoll 主从 Reactor），TCP 上以 **4 字节长度前缀分帧** 解决粘包
- 协议层：Protobuf，外层统一使用 `BaseMessage{type, payload}` 信封（定义见 `src/chatsystem/chat.proto`）
- 存储：MySQL 持久化用户/好友/群组/离线消息；Redis Pub/Sub 做跨节点消息广播 + SET 维护全局在线状态
- 负载均衡：Nginx stream 模块 TCP least_conn
- 安全：OpenSSL SHA256 加盐哈希存密码，禁止明文入库

详细文档见 `README.md`（协议枚举、错误码、数据库 ER 图、集群启动流程）。

## 构建与运行

```bash
# 1. 修改 proto 后重新生成（产物 chat.pb.h/cc 已入库）
cd src/chatsystem && protoc --proto_path=. --cpp_out=. chat.proto

# 2. 构建（输出到 bin/）
cmake -S . -B build && cmake --build build -j4

# 3. 初始化 MySQL 数据库
python3 sql/init_db.py          # 或 mysql -uroot -p < sql/01_create_tables.sql

# 4. 启动服务端（参数必须为 <ip> <port>）
./bin/log_threadpool_test 127.0.0.1 6000

# 5. 运行端到端测试客户端（含 12 个用例，输出 PASS/FAIL 汇总）
./bin/client 127.0.0.1 6000     # 直连节点；或连 8080 走 Nginx

# 压测工具
python3 tools/chat_stress.py    # 用法见 stress_report.md
```

依赖：libmuduo-dev、libboost-all-dev、libmysqlclient-dev、libssl-dev、libhiredis-dev、libprotobuf-dev、protobuf-compiler。运行时依赖 Redis / MySQL /（可选）Nginx。

## 项目结构

```
src/chatsystem/        # 聊天服务核心，编译为静态库 muduo_chat
├── chatserver.hpp/cpp      # muduo TcpServer 封装 + 4字节长度前缀分帧(onMessage)
├── chatservice.hpp/cpp     # 业务路由单例(instance())，全部 handler 实现
├── chat.proto / chat.pb.*  # 协议定义与生成产物
├── User.hpp / UserModel    # 用户 ORM 与表操作
├── FriendModel / Group(+Model) / OfflineMsgModel  # 其余业务表操作
├── PwdUtils                # SHA256 加盐哈希 + 校验
├── RedisMgr                # hiredis Pub/Sub(SUBSCRIBE独立线程) + 在线SET
└── mysql                   # MySQL 连接池封装（连接配置硬编码于 mysql.hpp）

src/logSystem/         # 自研日志库 muduo_log（AsyncLogging 双缓冲前端后端）
src/threadpool/        # 头文件线程池集合（Fixed/Work/Cache/Schedule + SyncQueue 系列）
tests/
├── TestLogThreadPool.cpp   # 服务端入口 main()：初始化 AsyncLogging → initRedis() → ChatServer
└── Client.cpp              # 端到端测试客户端（12 用例）
sql/                   # 建表脚本 + Python 初始化脚本
config/nginx_chat.conf # Nginx stream 层负载均衡配置
tools/chat_stress.py   # 压测脚本
bin/                   # 构建产物输出目录
logmsg/ser|cli         # 运行时日志落盘目录（gitignored）
```

## 架构要点

### IO 线程 / 工作线程分离（核心纪律）

IO 线程只做协议解析和参数校验，**所有 MySQL 操作必须派发到 `threadpool_.run(...)`**（chatservice 内置 muduo 自带 `ThreadPool`，4 个工作线程）。工作线程完成后通过 `loop->runInLoop([connPtr]{ connPtr->send(...); })` 切回 IO 线程发送响应，禁止在工作线程直接操作连接。

注意：聊天服务用的是 muduo 自带 ThreadPool，`src/threadpool/` 下是独立实现的自研线程池库（仅编译为 INTERFACE 目标 `lib::muduo_threadpool`，两者互不依赖）。

### 消息三级路由（oneChat / groupChat）

1. 本节点 `_userConnMap` 命中 → 直接 send（加锁访问）
2. 未命中 → Redis `SISMEMBER chat:online_users` 判断在线 → `PUBLISH chat:cross_node` 由目标节点的 SUBSCRIBE 线程接收转发
3. 离线 → 写 MySQL `OfflineMessage` 表，登录时按 `last_ack_seq` 水位拉取补投

### 消息有序与去重（v1.6 改造点）

每条私聊/群聊携带 `msg_id + seq`；ACK 回传 seq 让客户端推进水位；登录请求带 `last_ack_seq` 过滤已确认旧消息；跨节点 PUBLISH 直接转发不二次去重；DB 唯一索引 `(from_id, msg_id)` 作为最终防线；相关共享状态用互斥锁保护（分布式锁改造见提交历史）。

### 新增业务 Handler 流程

1. `chat.proto` 新增 `EnMsgType` 枚举值 + Request/Ack 消息 → 重新执行 protoc
2. `chatservice.hpp` 声明 handler，构造函数中注册进 `_msgHandlerMap`
3. handler 内：IO 线程校验参数 → DB 操作放 `threadpool_.run` → 结果经 `runInLoop` 回写

### 其他约定

- `chatservice` 是单例（`instance()`），服务端 main 必须先调 `initRedis()` 再 `ChatServer::start()`
- MySQL 连接参数硬编码在 `src/chatsystem/mysql.hpp`，Redis 参数在 `RedisMgr.cpp`
- 日志宏 `WT_LOG_INFO/ERROR << ...`（自研 logSystem），异步落盘到 `logmsg/`
- 提交信息、代码注释、文档均使用中文
