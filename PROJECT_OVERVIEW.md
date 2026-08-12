# ChatSystem 项目说明文档

> 基于 muduo C++ Reactor 模型的 Linux 集群版聊天室，集成 AI 智能客服与 RAG 知识检索

---

## 一、项目概述

本项目是一个完整的分布式聊天系统，核心采用 **muduo C++ Reactor** 网络模型构建高并发聊天服务，通过 **Protobuf** 进行通信协议序列化，**MySQL** 持久化业务数据，**Redis** 实现集群间消息同步。系统额外集成了基于 **FastAPI + Ollama + ChromaDB** 的 AI 智能客服模块，支持将聊天历史作为文本向量进行语义检索，为用户提供智能问答服务。

### 核心特性

| 维度 | 技术选型 | 说明 |
|------|----------|------|
| 网络框架 | muduo (Reactor + ThreadPool) | 非阻塞 IO + 工作线程池异步处理 DB 操作 |
| 通信协议 | Protobuf 3 | 二进制紧凑序列化，`BaseMessage` 信封 + `payload` 承载 |
| 数据库 | MySQL 8.0 (utf8mb4) | 用户/好友/群组/离线消息持久化 |
| 集群同步 | Redis Pub/Sub + SET | 跨节点消息广播 + 全局在线状态 |
| 负载均衡 | Nginx stream 模块 | TCP 层 least_conn 负载均衡 |
| 密码安全 | OpenSSL SHA256 + 随机盐 | `SHA256(salt + password)`，禁止明文入库 |
| AI 客服 | FastAPI + Ollama + ChromaDB | RAG 知识检索 + 语义记忆 |
| 构建系统 | CMake | 静态库 `muduo_chat` + 可执行目标 |

### 整体架构

```
                              ┌──────────────────────────────────┐
                              │          Nginx :8080             │
                              │   (TCP least_conn 负载均衡)       │
                              └────────┬────────────┬────────────┘
                                       │            │
                    ┌──────────────────┴──┐  ┌─────┴──────────────┐
                    │   ChatServer Node A │  │ ChatServer Node B  │
                    │   (muduo :6000)     │  │  (muduo :6001)    │
                    │  ┌───────────────┐  │  │ ┌───────────────┐  │
                    │  │ IO Thread(EPoll│  │  │ │IO Thread(EPoll│  │
                    │  │ + TcpServer)  │  │  │ │+ TcpServer)   │  │
                    │  ├───────────────┤  │  │ ├───────────────┤  │
                    │  │ ThreadPool    │  │  │ │ ThreadPool    │  │
                    │  │ (异步DB操作)   │  │  │ │ (异步DB操作)  │  │
                    │  └───────┬───────┘  │  │ └───────┬───────┘  │
                    └──────────┼──────────┘  └─────────┼─────────┘
                               │                       │
                    ┌──────────┴───────────────────────┘
                    │              │
                    ▼              ▼
              ┌──────────┐  ┌──────────────────┐
              │  Redis   │  │     MySQL        │
              │ Pub/Sub  │  │  (共享数据层)      │
              │ + SET    │  │                  │
              └──────────┘  └──────────────────┘

                    ┌──────────────────────────────────┐
                    │      AI 智能客服 (FastAPI :8001)  │
                    │  ┌────────┐  ┌────────────────┐  │
                    │  │Ollama  │  │   ChromaDB     │  │
                    │  │(LLM)  │  │  (向量存储)     │  │
                    │  └────────┘  └────────────────┘  │
                    │  ┌────────────────────────────┐  │
                    │  │  RAG Engine + 语义记忆      │  │
                    │  │  (聊天历史向量化检索)        │  │
                    │  └────────────────────────────┘  │
                    └──────────────────────────────────┘
```

---

## 二、目录结构

```
ThreadPoolAction/
├── chatsystem/                # C++ 聊天服务核心
│   ├── chatserver.cpp/hpp     # muduo TcpServer 封装
│   ├── chatservice.cpp/hpp    # 业务路由 + handler 实现
│   ├── chat.proto             # Protobuf 协议定义
│   ├── chat.pb.h/cc           # protoc 生成产物
│   ├── User.hpp               # 用户 ORM
│   ├── UserModel.cpp/hpp      # 用户表操作（注册/登录/查询）
│   ├── PwdUtils.cpp/hpp       # 密码加密工具（SHA256+盐）
│   ├── FriendModel.cpp/hpp    # 好友关系表操作
│   ├── Group.hpp              # 群组 ORM
│   ├── GroupModel.cpp/hpp     # 群组+群成员表操作
│   ├── OfflineMsgModel.cpp/hpp# 离线消息表操作
│   ├── RedisMgr.cpp/hpp      # Redis Pub/Sub + 在线SET管理
│   ├── mysql.cpp/hpp          # MySQL 连接池封装
│   ├── CMakeLists.txt         # 子项目构建
│   └── README.md              # 模块说明
│
├── src/chat_ai/               # AI 智能客服模块
│   ├── main.py                # FastAPI 入口
│   ├── config.py              # pydantic-settings 配置
│   ├── runtime_config.py      # 热可调运行时参数
│   ├── routers/               # API 路由层
│   │   ├── chat.py            # SSE 流式对话
│   │   ├── history.py         # 会话历史管理
│   │   ├── index_routes.py    # 文件上传与索引
│   │   ├── models.py          # 模型发现
│   │   ├── settings.py        # 运行时配置 API
│   │   └── health.py          # 健康检查
│   ├── services/              # 业务逻辑层
│   │   ├── stream_engine.py   # SSE 流式对话引擎
│   │   ├── rag_engine.py      # RAG 检索编排
│   │   ├── memory.py          # 语义记忆（ChromaDB）
│   │   ├── compress.py        # 历史压缩
│   │   ├── web_search.py      # 联网搜索
│   │   ├── prompt_builder.py  # 提示词构建
│   │   └── providers/         # 适配器层
│   ├── database/              # SQLite 数据层
│   ├── models/schemas.py      # Pydantic 模型
│   ├── frontend/              # React 前端
│   ├── .env.example           # 环境配置示例
│   ├── CONTEXT.md             # 领域语言说明
│   └── README.md              # AI 模块详细文档
│
├── sql/
│   ├── 01_create_tables.sql   # MySQL 完整建表脚本
│   └── init_db.py             # 数据库初始化脚本
│
├── config/
│   └── nginx_chat.conf        # Nginx TCP 负载均衡配置
│
├── tests/
│   ├── Client.cpp             # 端到端测试客户端
│   └── TestLogThreadPool.cpp  # 服务端启动入口
│
├── json.hpp                   # nlohmann/json 头文件
├── third_party/               # 第三方依赖
└── CMakeLists.txt             # 顶层构建
```

---

## 三、数据库设计

### ER 关系图

```
┌──────────────┐       ┌──────────────┐
│    User      │       │   Friend     │
│──────────────│       │──────────────│
│ id (PK)      │<──┐   │ userid   ────┼─→ User.id
│ name (UNIQUE)│   │   │ friendid ────┼─→ User.id
│ password     │   │   │ created_at   │
│ salt         │   │   └──────────────┘
│ state        │   │
│ created_at   │   │   ┌──────────────┐
│ updated_at   │   │   │  GroupInfo   │
└──────┬───────┘   │   │──────────────│
       │           │   │ id (PK)      │
       │           │   │ group_name   │
       │           │   │ group_desc   │
       │           │   │ creator_id ──┼─→ User.id
       │           │   │ created_at   │
       │           │   └──────┬───────┘
       │           │          │
       │           │          │ 1:N
       │           │          ▼
       │           │   ┌──────────────┐
       │           └───┤ GroupMember  │
       │               │──────────────│
       │               │ groupid (PK)─┼─→ GroupInfo.id
       └───────────────┤ userid  (PK)─┼─→ User.id
                       │ role         │
                       │ join_time    │
                       └──────────────┘

┌──────────────────┐
│ OfflineMessage   │
│──────────────────│
│ id (PK)          │
│ userid     ──────┼─→ User.id
│ from_id          │
│ msg_type         │
│ content          │
│ created_at       │
└──────────────────┘
```

### 表说明

| 表名 | 用途 | 关键索引 |
|------|------|----------|
| `User` | 用户基础信息 + 密码哈希 + 在线状态 | `UNIQUE(name)` |
| `Friend` | 好友关系（单向存储，应用层维护双向） | `PK(userid,friendid)` + `idx(friendid)` |
| `GroupInfo` | 群组信息 | `idx(creator_id)` |
| `GroupMember` | 群成员关联 | `PK(groupid,userid)` + `idx(userid)` |
| `OfflineMessage` | 离线消息存储 | `idx(userid)` |

### 集群扩展性设计

1. **所有主键 BIGINT** — 后续可替换为雪花算法等分布式 ID，无需改表结构
2. **节点无状态** — 服务节点不持有 DB 连接状态，通过 `MysqlConnPool` 连接池共享访问
3. **state 字段预留 Redis 共享** — 当前存 MySQL，集群多节点时通过 Redis SET 保证在线状态全局一致
4. **离线消息表追加写** — 天然支持多节点并发写入

### 初始化命令

```bash
# Python 脚本（推荐，带验证）
python3 sql/init_db.py

# 或直接执行 SQL
mysql -uroot -p < sql/01_create_tables.sql
```

---

## 四、Protobuf 协议设计

### 消息类型枚举

```protobuf
enum EnMsgType {
    MSG_NONE = 0;              // 占位（proto3首项必须为0）
    LOGIN_MSG = 1;             // 登录请求
    LOGIN_MSG_ACK = 2;         // 登录响应
    LOGINOUT_MSG = 3;          // 注销
    REG_MSG = 4;               // 注册请求
    REG_MSG_ACK = 5;           // 注册响应
    ONE_CHAT_MSG = 6;          // 一对一私聊
    ONE_CHAT_MSG_ACK = 7;      // 私聊回执
    ADD_FRIEND_MSG = 8;       // 添加好友
    ADD_FRIEND_MSG_ACK = 9;   // 添加好友响应
    DEL_FRIEND_MSG = 10;       // 删除好友
    DEL_FRIEND_MSG_ACK = 11;  // 删除好友响应
    CREATE_GROUP_MSG = 12;    // 创建群组
    CREATE_GROUP_MSG_ACK = 13;// 创建群组响应
    ADD_GROUP_MSG = 14;       // 加入群组
    ADD_GROUP_MSG_ACK = 15;   // 加入群组响应
    QUIT_GROUP_MSG = 16;      // 退出群组
    QUIT_GROUP_MSG_ACK = 17;  // 退出群组响应
    GROUP_CHAT_MSG = 18;       // 群聊
    GROUP_CHAT_MSG_ACK = 19;  // 群聊回执
    CROSS_NODE_CHAT_MSG = 20; // 跨节点消息转发
}
```

### 通信信封

所有消息使用 `BaseMessage` 作为外层信封：

```protobuf
message BaseMessage {
    EnMsgType type = 1;     // 业务类型
    bytes payload = 2;      // 具体业务消息序列化字节
}
```

### 错误码体系

| code | 含义 | 适用场景 |
|------|------|----------|
| 0 | 成功 | 所有响应 |
| 1 | 参数非法 | 注册/登录参数校验失败 |
| 2 | 用户名已存在 | 注册查重失败 |
| 3 | 数据库写入失败 | DB 操作异常 |
| 4 | 用户不存在 | 登录/添加好友目标不存在 |
| 5 | 密码错误 | 登录校验失败 |
| 6 | 已是好友/群成员 | 重复操作 |
| 7 | 权限不足 | 群操作权限校验 |

### protoc 编译命令

```bash
cd chatsystem
protoc --proto_path=. --cpp_out=. chat.proto
```

---

## 五、C++ 聊天服务

### 5.1 网络模型

基于 muduo Reactor 模型：

- **主 Reactor (IO Thread)**：EPoll 监听新连接，分发给 SubReactor
- **SubReactor (IO Threads)**：处理已连接 socket 的读写事件
- **ThreadPool (工作线程)**：异步执行 DB 操作，绝对不阻塞 IO 线程

```
客户端连接 → EPoll → IO Thread(解析BaseMessage)
                        ↓
                    路由到 handler
                        ↓
            ┌──────────┴──────────┐
            │                     │
     轻量操作(直接执行)    重DB操作(threadpool_.run)
            │                     │
            │             ┌───────┴───────┐
            │             │  工作线程执行   │
            │             │  - queryByName │
            │             │  - insert      │
            │             │  - 密码哈希     │
            │             └───────┬───────┘
            │                     │
            └─────────┬───────────┘
                      ▼
          loop->runInLoop(conn->send)
          (切回IO线程发送响应)
```

### 5.2 异步数据库设计

**核心原则**：所有 MySQL 操作在 `threadpool_` 工作线程执行，IO 线程仅做协议解析与参数校验。

```cpp
// 示例：注册流程异步化
void chatservice::reg(...) {
    // IO线程：参数校验（无DB访问）
    if (name.empty() || password.empty()) {
        // 立即同步回送错误
        return;
    }
    // 切换到工作线程
    threadpool_.run([this, name, password, connPtr]() {
        // 工作线程：DB操作
        if (_userModel.queryByName(name)) {
            // 用户名重复
        }
        auto salt = PwdUtils::generateSalt();
        auto hash = PwdUtils::sha256(salt + password);
        _userModel.insert(user);
        // 切回IO线程发送响应
        loop->runInLoop([connPtr, buf]() {
            connPtr->send(buf);
        });
    });
}
```

### 5.3 业务 Handler 清单

| Handler | 消息类型 | 业务流程 |
|---------|----------|----------|
| `login` | LOGIN_MSG | 查询用户 → 哈希校验密码 → 更新state=online → 记录connMap → 投递离线消息 |
| `reg` | REG_MSG | 参数校验 → 查重 → 加盐哈希 → 写入DB → 返回新ID |
| `oneChat` | ONE_CHAT_MSG | 三级路由：本节点→Redis Pub/Sub→离线存储 |
| `addFriend` | ADD_FRIEND_MSG | 校验目标存在 → 双向插入Friend表 |
| `delFriend` | DEL_FRIEND_MSG | 双向删除Friend表 |
| `createGroup` | CREATE_GROUP_MSG | 插入GroupInfo → 创建者加入GroupMember |
| `addGroup` | ADD_GROUP_MSG | 查重 → 插入GroupMember |
| `quitGroup` | QUIT_GROUP_MSG | 删除GroupMember记录 |
| `groupChat` | GROUP_CHAT_MSG | 查群成员 → 逐个三级路由 → 回执统计 |
| `loginOut` | LOGINOUT_MSG | 移除connMap → 更新state=offline |
| `clientCloseException` | (连接断开) | 反向查找conn → 移除connMap → 更新state |

### 5.4 密码安全方案

```
注册时：
  1. generateSalt() → 32位hex随机盐（OpenSSL RAND_bytes）
  2. sha256(salt + password) → 64位hex哈希
  3. DB存储：password=哈希值, salt=盐值

登录时：
  1. query(id) → 获取salt和password哈希
  2. sha256(salt + inputPassword) → 计算哈希
  3. 比较两个哈希值 → verify()
```

---

## 六、集群架构

### 6.1 Redis 数据结构

| Key | 类型 | 用途 |
|-----|------|------|
| `chat:online_users` | SET | 全局在线用户ID集合（login SADD / logout SREM） |
| `chat:cross_node` | Pub/Sub Channel | 跨节点消息转发通道 |

### 6.2 消息三级路由

```
oneChat(fromId → toId) / groupChat(fromId → group)
    │
    ├─ 1. 查本节点 _userConnMap
    │     └─ 命中 → conn->send() 直接转发（零延迟）
    │
    ├─ 2. 本节点未命中 → 查 Redis SISMEMBER chat:online_users toId
    │     ├─ 在线 → PUBLISH chat:cross_node CrossNodeMsg
    │     │         └─ 目标节点SUBSCRIBE收到 → 查本地_userConnMap → conn->send()
    │     └─ 离线 → MySQL OfflineMessage 存储离线消息
    │                └─ 用户登录时异步拉取并投递
```

### 6.3 Nginx 负载均衡

聊天系统使用原生 TCP（Protobuf over TCP），使用 nginx `stream` 模块进行 TCP 负载均衡：

```nginx
stream {
    upstream chat_backend {
        least_conn;                              # 最少连接数算法
        server 127.0.0.1:6000 max_fails=3 fail_timeout=30s;
        server 127.0.0.1:6001 max_fails=3 fail_timeout=30s;
    }
    server {
        listen 8080;                             # 对外端口
        proxy_pass chat_backend;
        proxy_connect_timeout 10s;
        proxy_timeout 300s;                      # 长连接超时
    }
}
```

**依赖**：`nginx` + `libnginx-mod-stream`

---

## 七、AI 智能客服模块

### 7.1 技术栈

| 组件 | 技术 | 说明 |
|------|------|------|
| Web 框架 | FastAPI | 异步 ASGI，SSE 流式推送 |
| LLM 推理 | Ollama | 本地模型推理，支持 qwen3.5/deepseek-r1 |
| 向量存储 | ChromaDB | 嵌入向量持久化与相似度检索 |
| 嵌入模型 | bge-m3 | 文本向量化 |
| 数据库 | SQLite (aiosqlite) | 会话与消息持久化 |
| 前端 | React 19 + Vite + Tailwind | 类 ChatGPT 界面 |

### 7.2 核心能力

| 功能 | 说明 |
|------|------|
| 流式对话 | SSE 逐字推送，10 步编排流水线 |
| RAG 知识库 | 上传文件 → 分块 → 向量检索 → HyDE → BM25 重排序 |
| 语义记忆 | 对话嵌入 ChromaDB，跨轮次语义检索历史 |
| 联网搜索 | DuckDuckGo / Tavily 双 Provider |
| 历史压缩 | 混合裁剪 + 增量摘要，水位线机制 |
| 多模型支持 | Ollama 本地 + OpenAI 兼容 API |

### 7.3 分层架构

```
路由层 (routers/) → 编排层 (services/) → 存储层 (database / vector store)
                        ↓
                   模型提供商 (Ollama / OpenAI)
```

### 7.4 关键 Seam（适配器）

| Seam | 适配器 | 用途 |
|------|--------|------|
| `VectorStore` | `ChromaAdapter` | 向量存储的持久化与检索 |
| `ModelProvider` | `OllamaProvider`, `OpenAIProvider` | 流式 token 生成 |
| `EmbeddingProvider` | `OllamaEmbeddingProvider` | 文本向量化 |
| `SearchProvider` | `DuckDuckGoProvider`, `TavilyProvider` | 联网搜索 |
| `TextExtractor` | `MarkItDown` | 文件文本提取 |

### 7.5 与聊天室集成方案

```
用户发送AI_CHAT_MSG
      ↓
C++ ChatServer (IO线程解析)
      ↓
threadpool_.run (工作线程)
      ├─ 1. 从MySQL查询用户历史聊天记录
      ├─ 2. HTTP POST 到 AI Service :8001
      │     body: {user_id, message, history: [...]}
      ├─ 3. AI Service:
      │     ├─ 生成消息embedding
      │     ├─ ChromaDB语义检索相关历史
      │     ├─ RAG检索知识库
      │     ├─ Ollama生成回复
      │     └─ 返回JSON {reply, session_id}
      ├─ 4. 解析AI响应
      └─ 5. loop->runInLoop → conn->send (AI_CHAT_ACK)
```

### 7.6 AI 服务配置

```bash
# .env 核心配置
OLLAMA_HOST=http://localhost:11434
OLLAMA_MODEL=qwen3.5:9B
EMBEDDING_MODEL=bge-m3
DB_PATH=data/chat.db
CHROMA_PERSIST_DIR=data/chroma_db
PORT=8001
```

---

## 八、构建与部署

### 8.1 依赖安装

```bash
# C++ 依赖
sudo apt-get install -y \
    cmake g++ \
    libmuduo-dev libboost-all-dev \
    libmysqlclient-dev \
    libssl-dev \
    libhiredis-dev \
    libprotobuf-dev protobuf-compiler

# Nginx 负载均衡
sudo apt-get install -y nginx libnginx-mod-stream

# Redis
sudo apt-get install -y redis-server

# AI 服务依赖
pip install -r src/chat_ai/requirements.txt

# Ollama 模型
ollama pull qwen3.5:9B
ollama pull bge-m3
```

### 8.2 构建命令

```bash
# 1. 编译 Protobuf
cd chatsystem
protoc --proto_path=. --cpp_out=. chat.proto

# 2. CMake 构建
cd /home/wangt/ThreadPoolAction/build
cmake ..
make -j4
```

构建产物：
- `bin/log_threadpool_test` — 聊天服务端
- `bin/client` — 测试客户端

### 8.3 集群启动流程

```bash
# 1. 启动基础设施
sudo systemctl start redis-server
sudo systemctl start nginx

# 2. 初始化数据库
python3 sql/init_db.py

# 3. 启动 AI 服务（可选）
cd src/chat_ai
python3 main.py &

# 4. 启动两个聊天服务节点
./bin/log_threadpool_test 127.0.0.1 6000 &
./bin/log_threadpool_test 127.0.0.1 6001 &

# 5. 客户端连接 nginx 负载均衡端口
./bin/client 127.0.0.1 8080
```

### 8.4 单节点启动

```bash
python3 sql/init_db.py
./bin/log_threadpool_test 127.0.0.1 6000
# 另一个终端
./bin/client 127.0.0.1 6000
```

---

## 九、测试

### 9.1 测试覆盖

`tests/Client.cpp` 包含 12 个端到端测试用例：

| # | 测试用例 | 验证点 |
|---|----------|--------|
| 1 | 注册新用户 | 异步DB查重 + 密码加盐哈希 + 写入 + 返回新ID |
| 2 | 登录用户 | PwdUtils::verify 哈希校验 + state更新 |
| 3 | 错误密码登录 | 哈希校验失败 + 返回错误码 |
| 4 | 重复注册用户名 | queryByName 查到已存在 |
| 5 | 空用户名注册 | 参数校验 |
| 6 | 添加好友 | 双向插入Friend表 |
| 7 | 删除好友 | 双向删除Friend表 |
| 8 | 创建群组 | 插入GroupInfo + 创建者加入GroupMember |
| 9 | 加入群组 | 查重 + 插入GroupMember |
| 10 | 私聊(离线存储) | OfflineMessage 存储 |
| 11 | 群聊(离线存储) | 群成员遍历 + 离线存储 |
| 12 | 注销 + 接收离线消息 | state更新 + 离线消息拉取投递 |

### 9.2 运行测试

```bash
# 启动服务端
./bin/log_threadpool_test 127.0.0.1 6000 &

# 运行客户端测试
./bin/client 127.0.0.1 6000

# 通过 nginx 负载均衡测试
./bin/client 127.0.0.1 8080
```

预期输出：
```
============================================================
 测试结果汇总
============================================================
  [PASS]  注册新用户
  [PASS]  正确凭据登录
  ...
  [PASS]  接收离线消息
============================================================
 通过: 12/12
============================================================
```

---

## 十、配置文件

### 10.1 MySQL 配置

配置位于 `chatsystem/mysql.hpp`：

| 参数 | 默认值 |
|------|--------|
| host | 127.0.0.1 |
| port | 3306 |
| user | root |
| password | ****** |
| dbname | chat |

### 10.2 Redis 配置

配置位于 `chatsystem/RedisMgr.cpp`：

| 参数 | 默认值 |
|------|--------|
| host | 127.0.0.1 |
| port | 6379 |
| online_set_key | chat:online_users |
| pubsub_channel | chat:cross_node |

### 10.3 Nginx 配置

配置位于 `config/nginx_chat.conf`，部署到 `/etc/nginx/nginx.conf`：

| 参数 | 值 |
|------|-----|
| 监听端口 | 8080 |
| 后端节点 | 127.0.0.1:6000, 127.0.0.1:6001 |
| 负载算法 | least_conn |
| 健康检查 | max_fails=3, fail_timeout=30s |
| 空闲超时 | 300s |

### 10.4 AI 服务配置

配置位于 `src/chat_ai/.env`（从 `.env.example` 复制）：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| OLLAMA_HOST | http://localhost:11434 | Ollama 服务地址 |
| OLLAMA_MODEL | qwen3.5:9B | 对话模型 |
| EMBEDDING_MODEL | bge-m3 | 嵌入模型 |
| PORT | 8001 | FastAPI 端口 |

---

## 十一、扩展性预留

### 11.1 Proto 消息类型预留

后续业务可直接扩展以下方向：
- AI 客服对话（AI_CHAT_MSG / AI_CHAT_ACK）
- 文件传输
- 消息已读回执
- 好友请求审核流程

### 11.2 数据库扩展

- `Friend` 表可增加 `status` 字段支持好友请求审核
- `OfflineMessage` 表已支持私聊与群聊两种类型
- 所有主键 BIGINT，可无缝替换为分布式 ID

### 11.3 集群扩展

- 新增节点只需启动新实例并加入 nginx upstream
- Redis 自动同步在线状态与跨节点消息
- MySQL 连接池支持多节点并发访问

---

## 十二、技术选型说明

### 为什么选择 muduo？

- **Reactor 模型**：EPoll + 非阻塞 IO，单线程处理数万并发连接
- **ThreadPool**：IO 线程与工作线程分离，DB 操作不阻塞网络线程
- **成熟稳定**：陈硕出品，工业级验证，适合 Linux 服务端

### 为什么选择 Protobuf？

- **二进制紧凑**：比 JSON 小 3-10 倍，节省带宽
- **强类型**：编译时检查，减少运行时错误
- **前向兼容**：新增字段不破坏旧客户端

### 为什么选择 Redis Pub/Sub？

- **低延迟**：毫秒级跨节点消息传递
- **无持久化**：仅用于实时在线消息广播，离线消息存 MySQL
- **轻量**：相比 Kafka/RabbitMQ，部署简单，适合聊天场景

### 为什么选择 Nginx stream 模块？

- **TCP 层负载均衡**：聊天协议是原生 TCP，非 HTTP
- **least_conn 算法**：适合长连接场景，均衡连接数
- **健康检查**：max_fails + fail_timeout 自动剔除故障节点

### 为什么 AI 模块使用 FastAPI？

- **异步原生**：ASGI + async/await，与 Ollama 异步调用完美契合
- **SSE 流式**：逐字推送，提升用户体验
- **自动文档**：OpenAPI/Swagger 开箱即用

---

## 十三、常见问题

### Q1: 注册用户无法登录？

检查 `UserModel::query(id)` 是否正确读取了 `salt` 字段。登录密码校验使用 `PwdUtils::verify(password, salt, stored_hash)`，而非明文比较。

### Q2: 集群消息丢失？

Pub/Sub 仅用于实时在线消息转发。如果目标用户不在线，消息会存入 MySQL `OfflineMessage` 表，用户登录时自动拉取。

### Q3: nginx 报 "stream directive not found"？

需安装 `libnginx-mod-stream` 包，并在 `nginx.conf` 顶部加载：
```nginx
load_module /usr/lib/nginx/modules/ngx_stream_module.so;
```

### Q4: AI 服务启动失败？

检查：
1. Ollama 守护进程是否运行：`ollama serve`
2. 模型是否已拉取：`ollama list`
3. 端口 8001 是否被占用

### Q5: 如何新增业务 Handler？

1. 在 `chat.proto` 新增消息类型枚举 + Request/Ack 结构体
2. 执行 `protoc --cpp_out=. chat.proto` 重新编译
3. 在 `chatservice.hpp` 声明 handler 方法
4. 在 `chatservice.cpp` 构造函数注册到 `_msgHandlerMap`
5. 实现 handler 方法（异步 DB 操作走 `threadpool_.run`）

---

## 十四、版本记录

| 版本 | 内容 |
|------|------|
| v1.0 | 登录/注册模块 + MySQL 建表 |
| v1.1 | 注册完善：密码加盐哈希、异步DB、参数校验 |
| v1.2 | 好友/群组/私聊/群聊/离线消息全业务 |
| v1.3 | Redis Pub/Sub 集群消息同步 |
| v1.4 | Nginx TCP 负载均衡 |
| v1.5 | AI 智能客服模块集成（FastAPI + Ollama + ChromaDB） |

---

## 十五、参考资料

- [muduo 网络库](https://github.com/chenshuo/muduo)
- [Protobuf 3 语法指南](https://protobuf.dev/programming-guides/proto3/)
- [FastAPI 官方文档](https://fastapi.tiangolo.com/)
- [Ollama 官方文档](https://ollama.com/)
- [ChromaDB 文档](https://docs.trychroma.com/)
- [Nginx Stream 模块](https://nginx.org/en/docs/stream/)
