# ChatSystem - 基于muduo的集群版聊天室

基于 muduo C++ Reactor 模型开发的 Linux 集群版聊天室，使用 Protobuf 作为通信协议，MySQL 持久化用户数据，Redis Pub/Sub 实现跨节点消息同步。

## 技术栈

| 组件 | 技术选型 | 用途 |
|------|----------|------|
| 网络框架 | muduo (Reactor + ThreadPool) | 非阻塞IO + 工作线程池 |
| 通信协议 | Protobuf 3 | 二进制序列化，高效紧凑 |
| 持久化 | MySQL 8.0 | 用户/好友/群组/离线消息存储 |
| 跨节点同步 | Redis Pub/Sub + SET | 实时消息广播 + 全局在线状态 |
| 密码安全 | OpenSSL SHA256 + 随机盐 | 防止明文存储、防御彩虹表 |
| 构建系统 | CMake 3.10+ | 跨平台构建 |

## 项目结构

```
chatsystem/
├── chat.proto              # Protobuf 协议定义（消息类型 + 请求/响应结构体）
├── chat.pb.h / chat.pb.cc  # protoc 生成的 C++ 代码
│
├── chatserver.hpp / .cpp   # muduo TCP 服务器封装
├── chatservice.hpp / .cpp   # 业务路由 + 全部 handler 实现
│
├── mysql.hpp / .cpp         # MySQL 连接 + 连接池封装
├── UserModel.hpp / .cpp     # 用户表 ORM
├── FriendModel.hpp / .cpp  # 好友关系表 ORM
├── GroupModel.hpp / .cpp    # 群组 + 群成员表 ORM
├── OfflineMsgModel.hpp / .cpp # 离线消息表 ORM
├── RedisMgr.hpp / .cpp     # Redis 管理（PUBLISH/SUBSCRIBE/在线SET）
│
├── User.hpp                # 用户 ORM 类
├── Group.hpp                # 群组 + 群成员 ORM 类
├── PwdUtils.hpp / .cpp     # 密码工具（SHA256 + 随机盐）
│
├── AsyncLogging.hpp         # 异步日志（muduo 组件）
├── Logger.hpp               # 日志接口
│
└── CMakeLists.txt           # 构建配置
```

## 数据库设计

### ER 关系图

```
┌──────────┐     ┌──────────┐     ┌──────────────┐
│   User   │     │  Friend  │     │  GroupInfo   │
├──────────┤     ├──────────┤     ├──────────────┤
│ id (PK)  │◄──┐ │ userid   │──┐  │ id (PK)      │
│ name     │   └─│ friendid │  │  │ group_name   │
│ password │     └──────────┘  │  │ group_desc   │
│ salt     │                   │  │ creator_id   │
│ state    │                   │  └──────┬───────┘
└──────────┘                   │         │
                               │  ┌──────┴───────┐
                               └──│ GroupMember  │
                                  ├──────────────┤
                                  │ groupid (PK) │
                                  │ userid  (PK) │
                                  │ role         │
                                  └──────────────┘

┌──────────────────┐
│ OfflineMessage   │
├──────────────────┤
│ id (PK)          │
│ userid           │
│ from_id          │
│ msg_type         │
│ content          │
└──────────────────┘
```

### 表说明

| 表名 | 用途 | 关键索引 |
|------|------|----------|
| `User` | 用户基本信息 | `UNIQUE(name)` |
| `Friend` | 好友关系（双向存储） | 复合主键 `(userid, friendid)` + `idx(friendid)` |
| `GroupInfo` | 群组信息 | `idx(creator_id)` |
| `GroupMember` | 群成员关系 | 复合主键 `(groupid, userid)` + `idx(userid)` |
| `OfflineMessage` | 离线消息 | `idx(userid)` |

建表 SQL 位于 `sql/01_create_tables.sql`，初始化脚本为 `sql/init_db.py`。

## 通信协议

### 消息类型枚举

```protobuf
enum EnMsgType {
    MSG_NONE = 0;               // 占位
    LOGIN_MSG = 1;              // 登录请求
    LOGIN_MSG_ACK = 2;          // 登录响应
    LOGINOUT_MSG = 3;           // 注销
    REG_MSG = 4;                // 注册请求
    REG_MSG_ACK = 5;            // 注册响应
    ONE_CHAT_MSG = 6;           // 一对一私聊
    ONE_CHAT_ACK = 7;           // 私聊回执
    ADD_FRIEND_MSG = 8;         // 添加好友
    ADD_FRIEND_ACK = 9;         // 添加好友回执
    DEL_FRIEND_MSG = 10;        // 删除好友
    DEL_FRIEND_ACK = 11;        // 删除好友回执
    CREATE_GROUP_MSG = 12;      // 创建群组
    CREATE_GROUP_ACK = 13;      // 创建群组回执
    ADD_GROUP_MSG = 14;         // 加入群组
    ADD_GROUP_ACK = 15;          // 加入群组回执
    QUIT_GROUP_MSG = 16;        // 退出群组
    QUIT_GROUP_ACK = 17;        // 退出群组回执
    GROUP_CHAT_MSG = 18;        // 群聊
    GROUP_CHAT_MSG_ACK = 19;    // 群聊回执
    CROSS_NODE_CHAT_MSG = 20;   // 跨节点消息转发（Redis载体）
}
```

### 传输格式

客户端与服务端之间传输 `BaseMessage` 的序列化字节流：

```protobuf
message BaseMessage {
    EnMsgType type = 1;    // 消息类型
    string payload = 2;    // 业务消息序列化字节
}
```

## 集群架构

### 多节点消息路由

```
                    ┌──────────────────────────────────────────┐
                    │              Redis Server                  │
                    │  ┌─────────────────┐  ┌────────────────┐   │
                    │  │ chat:cross_node │  │chat:online_users│   │
                    │  │   (Pub/Sub)     │  │    (SET)       │   │
                    │  └───────┬─────────┘  └───────┬────────┘   │
                    └──────────┼────────────────────┼────────────┘
                               │                    │
              ┌────────────────┼────────────────────┼────────────┐
              │                │                    │            │
     ┌────────┴───────┐ ┌──────┴───────┐  ┌────────┴──────┐ ┌────┴───────┐
     │    Node A      │ │   Node B     │  │   Node A      │ │  Node B    │
     │  muduo IO      │ │  muduo IO    │  │ _userConnMap  │ │_userConnMap│
     │  ThreadPool    │ │  ThreadPool  │  │   (local)     │ │  (local)  │
     └────────────────┘ └──────────────┘  └───────────────┘ └────────────┘
```

### 消息三级路由

私聊/群聊消息发送时，按以下顺序决策：

```
oneChat(fromId → toId) / groupChat(fromId → group)
    │
    ├─ 1. 查本节点 _userConnMap
    │     └─ 命中 → conn->send() 直接转发（零延迟）
    │
    ├─ 2. 本节点未命中 → 查 Redis SISMEMBER chat:online_users toId
    │     ├─ 在线 → PUBLISH chat:cross_node CrossNodeMsg
    │     │         └─ 目标节点 SUBSCRIBE 收到 → 查本地 _userConnMap → conn->send()
    │     │
    │     └─ 离线 → MySQL OfflineMessage 存储离线消息
    │                └─ 用户登录时异步拉取并投递
```

### Redis 数据结构

| Key | 类型 | 用途 |
|-----|------|------|
| `chat:online_users` | SET | 全局在线用户ID集合（login SADD / logout SREM） |
| `chat:cross_node` | Pub/Sub Channel | 跨节点消息转发通道（实时，无持久化） |

### CrossNodeMsg 协议

```protobuf
message CrossNodeMsg {
    int64 target_user_id = 1;   // 目标用户ID
    int32 msg_type = 2;          // 原始消息类型（ONE_CHAT_MSG / GROUP_CHAT_MSG）
    string payload = 3;         // 原始消息序列化字节
}
```

## 异步数据库设计

### IO 线程 vs 工作线程

```
muduo IO 线程（主循环）
├─ onMessage: 接收数据 → 解析 BaseMessage → 路由到 handler
├─ handler 轻量操作: 参数校验、协议解析（不阻塞）
├─ threadpool_.run([task]) → 派发到工作线程
│
muduo ThreadPool 工作线程（4线程）
├─ DB 操作: query / insert / update / delete
├─ Redis 操作: PUBLISH / SISMEMBER
├─ 构造响应 → conn->send()（muduo 内部 runInLoop 保证线程安全）
│
Redis SUBSCRIBE 线程（独立线程）
├─ 阻塞等待 chat:cross_node 频道消息
├─ 收到消息 → 查 _userConnMap → conn->send() 转发
```

### 关键约束

- **所有 MySQL 操作异步执行**：绝不阻塞 muduo IO 线程
- **Redis PUBLISH 异步执行**：在工作线程中调用
- **SUBSCRIBE 独立线程**：不占用 muduo 线程池
- **conn->send() 线程安全**：muduo 内部通过 `runInLoop` 跨线程调度

## 业务流程

### 注册流程

```
客户端 REG_MSG
    ↓
IO线程: 参数校验（用户名/密码非空、长度限制）
    ├─ 失败 → 立即返回 code=1
    ↓
ThreadPool: queryByName 查重
    ├─ 已存在 → code=2
    ├─ 不存在 → generateSalt + sha256(salt+password)
    │           → insert 新用户（password字段存哈希）
    │           ├─ 失败 → code=3
    │           └─ 成功 → code=0, 返回新用户ID
    ↓
conn->send() 返回 RegisterResponse
```

### 登录流程

```
客户端 LOGIN_MSG
    ↓
IO线程: query(id) 查用户 → PwdUtils::verify 校验密码
    ├─ 失败 → 返回 id=-1
    ├─ 成功 → updateState("online")
    │         → _userConnMap[id] = conn（本节点连接追踪）
    │         → Redis SADD chat:online_users id（全局在线标记）
    │         → 返回 LOGIN_MSG_ACK
    │         → ThreadPool 异步拉取离线消息并投递
    ↓
conn->send() 返回 LoginResponse
```

### 密码安全

```
注册时：
  salt = PwdUtils::generateSalt()          // 32位hex随机盐
  hashedPwd = PwdUtils::sha256(salt + 明文密码)
  DB存储: password = hashedPwd, salt = salt

登录时：
  PwdUtils::verify(明文密码, DB.salt, DB.password)
  = (sha256(salt + 明文密码) == DB.password)
```

## 构建与运行

### 依赖安装

```bash
# muduo
git clone https://github.com/chenshuo/muduo.git
cd muduo && cmake . && make -j4 && sudo make install

# protobuf
sudo apt-get install -y protobuf-compiler libprotobuf-dev

# mysql
sudo apt-get install -y libmysqlclient-dev

# openssl
sudo apt-get install -y libssl-dev

# hiredis
sudo apt-get install -y libhiredis-dev

# redis
sudo apt-get install -y redis-server
sudo systemctl start redis-server
```

### 编译

```bash
# 1. protoc 编译
cd chatsystem
protoc --proto_path=. --cpp_out=. chat.proto

# 2. CMake 构建
cd ../build
cmake ..
make -j4
```

生成产物：
- `bin/log_threadpool_test` — 聊天服务器
- `bin/client` — 测试客户端

### 初始化数据库

```bash
python3 sql/init_db.py -u root -p <密码>
```

### 启动服务

```bash
# 启动聊天服务器
./bin/log_threadpool_test 127.0.0.1 6000

# 运行测试客户端
./bin/client 127.0.0.1 6000
```

### 集群部署

```bash
# 节点A
./bin/log_threadpool_test 127.0.0.1 6000

# 节点B（同一台机器不同端口，或不同机器）
./bin/log_threadpool_test 127.0.0.1 6001

# 客户端连接任意节点
./bin/client 127.0.0.1 6000
./bin/client 127.0.0.1 6001
```

两个节点共享同一个 MySQL 和 Redis，通过 `chat:cross_node` 频道同步跨节点消息。

## 错误码体系

| code | 含义 | 使用场景 |
|------|------|----------|
| 0 | 成功 | 所有业务 |
| 1 | 参数非法 / 用户不存在 | 注册校验、添加好友目标不存在 |
| 2 | 用户名已存在 / 已是好友 | 注册查重、加入群组重复 |
| 3 | 数据库操作失败 | 注册写入失败 |
| -1 | 登录失败 / 未知错误 | 登录密码错误、注销 |

## 扩展性预留

| 预留接口 | 说明 |
|----------|------|
| `CrossNodeMsg` | 可扩展为承载任意消息类型的跨节点载体 |
| `OfflineMsg.msg_type` | 支持 private/group/未来新消息类型 |
| `GroupUser.role` | 区分 creator/normal/admin（预留管理员角色） |
| `User.state` | offline/online（预留 away/busy 等状态） |
| Redis `chat:online_users` | 集群扩展时可直接增加节点，SET 自动共享 |
| MySQL 表主键 BIGINT | 可替换为雪花算法等分布式ID |
