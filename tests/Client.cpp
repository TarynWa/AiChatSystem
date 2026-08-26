#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <string>
#include <vector>
#include "AsyncLogging.hpp"
#include "proto/chat.pb.h"

namespace fs = std::filesystem;
wangt::AsyncLogging *asynclog = nullptr;
void asyncWriteFile(const string &info) { asynclog->append(info); }
void asyncFlushFile() { asynclog->flush(); }

// 测试结果记录
struct TestResult
{
    string name;
    bool passed;
};

// ---------- 通信辅助函数 ----------

// 连接到服务器
int connectToServer(const string &ip, int port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) return -1;
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr);
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
    {
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// 发送一条带4字节长度前缀的 BaseMessage
bool sendFrame(int sockfd, const chat::BaseMessage &msg)
{
    string payload;
    if (!msg.SerializeToString(&payload)) return false;
    int32_t be32 = htonl(static_cast<int32_t>(payload.size()));
    string frame(reinterpret_cast<const char*>(&be32), sizeof(be32));
    frame += payload;
    int total = 0;
    while (total < (int)frame.size())
    {
        int n = send(sockfd, frame.data() + total, frame.size() - total, MSG_NOSIGNAL);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

// 从socket读取一帧（4字节长度+payload），解析为 BaseMessage
bool recvFrame(int sockfd, chat::BaseMessage &response, int timeout_sec = 5)
{
    // 读4字节长度前缀
    int32_t be32 = 0;
    int got = 0;
    while (got < (int)sizeof(be32))
    {
        fd_set readfds;
        struct timeval tv;
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        int rv = select(sockfd + 1, &readfds, nullptr, nullptr, &tv);
        if (rv <= 0) return false;
        int n = recv(sockfd, reinterpret_cast<char*>(&be32) + got, sizeof(be32) - got, 0);
        if (n <= 0) return false;
        got += n;
    }
    int32_t len = ntohl(be32);
    if (len <= 0 || len > 16 * 1024 * 1024) return false;

    // 读payload
    string payload(len, '\0');
    got = 0;
    while (got < len)
    {
        fd_set readfds;
        struct timeval tv;
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        int rv = select(sockfd + 1, &readfds, nullptr, nullptr, &tv);
        if (rv <= 0) return false;
        int n = recv(sockfd, &payload[got], len - got, 0);
        if (n <= 0) return false;
        got += n;
    }
    return response.ParseFromString(payload);
}

// 发送一条 BaseMessage 并接收服务端响应
bool sendAndRecv(int sockfd, const chat::BaseMessage &msg, chat::BaseMessage &response, int timeout_sec = 5)
{
    if (!sendFrame(sockfd, msg)) return false;
    return recvFrame(sockfd, response, timeout_sec);
}

// 尝试接收额外消息（用于接收离线消息投递），无消息则超时返回false
bool recvOptional(int sockfd, chat::BaseMessage &response, int timeout_sec = 2)
{
    return recvFrame(sockfd, response, timeout_sec);
}

// 构造并发送指定类型的消息
bool sendType(int sockfd, chat::EnMsgType type, const google::protobuf::Message &payload, chat::BaseMessage &response)
{
    chat::BaseMessage msg;
    msg.set_type(type);
    msg.set_payload(payload.SerializeAsString());
    return sendAndRecv(sockfd, msg, response);
}

// ---------- 客户端维护的 msg_id 与 seq ----------
// 每发送方单调递增，断线重连后不重置，确保分布式场景下消息有序
static int64_t g_msg_id_counter = 0;
static int32_t g_seq_counter = 0;
// 简易雪花算法：时间戳左移 + 自增计数，保证全局唯一且单调
static void nextMsgIdSeq(int64_t &out_msg_id, int32_t &out_seq)
{
    g_seq_counter += 1;
    g_msg_id_counter += 1;
    int64_t ts = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    // msg_id = (timestamp_ms << 20) | counter_low_20bits
    out_msg_id = (ts << 20) | (g_msg_id_counter & 0xFFFFF);
    out_seq = g_seq_counter;
}

// ---------- 测试用例 ----------

// 测试1：注册新用户
bool testRegister(int sockfd, const string &username, const string &password, int64_t &outUserId)
{
    WT_LOG_INFO << "===== [TEST] 注册用户: " << username << " =====";
    chat::RegisterRequest req;
    req.set_username(username);
    req.set_password(password);
    req.set_id(0);

    chat::BaseMessage response;
    if (!sendType(sockfd, chat::REG_MSG, req, response)) return false;
    if (response.type() != chat::REG_MSG_ACK) return false;

    chat::RegisterResponse ack;
    if (!ack.ParseFromString(response.payload())) return false;

    WT_LOG_INFO << "注册响应: code=" << ack.code() << " id=" << ack.id();
    if (ack.code() != 0) return false;
    outUserId = ack.id();
    return true;
}

// 测试2：登录
// 改造点：传入 last_ack_seq（断线重连时同步水位），默认0表示首次登录
bool testLogin(int sockfd, const string &username, const string &password, int64_t expectedId, int32_t last_ack_seq = 0)
{
    WT_LOG_INFO << "===== [TEST] 登录: " << username << " (id=" << expectedId << ") last_ack_seq=" << last_ack_seq << " =====";
    chat::LoginRequest req;
    req.set_username(username);
    req.set_password(password);
    req.set_id(expectedId);
    req.set_last_ack_seq(last_ack_seq);

    chat::BaseMessage response;
    if (!sendType(sockfd, chat::LOGIN_MSG, req, response)) return false;
    if (response.type() != chat::LOGIN_MSG_ACK) return false;

    chat::LoginRequest ack;
    if (!ack.ParseFromString(response.payload())) return false;
    WT_LOG_INFO << "登录响应: id=" << ack.id() << " last_ack_seq=" << ack.last_ack_seq();
    return ack.id() == expectedId;
}

// 测试3：添加好友
// 改造点：生成 msg_id 与 seq，与服务端去重窗口配合
bool testAddFriend(int sockfd, int fromId, int toId)
{
    WT_LOG_INFO << "===== [TEST] 添加好友: " << fromId << " -> " << toId << " =====";
    int64_t msg_id; int32_t seq;
    nextMsgIdSeq(msg_id, seq);
    chat::AddFriendRequest req;
    req.set_from_id(fromId);
    req.set_to_id(toId);
    req.set_msg_id(msg_id);
    req.set_seq(seq);

    chat::BaseMessage response;
    if (!sendType(sockfd, chat::ADD_FRIEND_MSG, req, response)) return false;
    if (response.type() != chat::ADD_FRIEND_MSG_ACK) return false;

    chat::AddFriendAck ack;
    ack.ParseFromString(response.payload());
    WT_LOG_INFO << "添加好友响应: code=" << ack.code() << " msg=" << ack.msg()
                << " msg_id=" << ack.msg_id() << " seq=" << ack.seq();
    return ack.code() == 0;
}

// 测试4：删除好友
bool testDelFriend(int sockfd, int fromId, int toId)
{
    WT_LOG_INFO << "===== [TEST] 删除好友: " << fromId << " -> " << toId << " =====";
    int64_t msg_id; int32_t seq;
    nextMsgIdSeq(msg_id, seq);
    chat::DelFriendRequest req;
    req.set_from_id(fromId);
    req.set_to_id(toId);
    req.set_msg_id(msg_id);
    req.set_seq(seq);

    chat::BaseMessage response;
    if (!sendType(sockfd, chat::DEL_FRIEND_MSG, req, response)) return false;
    if (response.type() != chat::DEL_FRIEND_MSG_ACK) return false;

    chat::DelFriendAck ack;
    ack.ParseFromString(response.payload());
    WT_LOG_INFO << "删除好友响应: code=" << ack.code() << " msg=" << ack.msg()
                << " msg_id=" << ack.msg_id() << " seq=" << ack.seq();
    return ack.code() == 0;
}

// 测试5：创建群组
bool testCreateGroup(int sockfd, int creatorId, const string &name, const string &desc, int64_t &outGroupId)
{
    WT_LOG_INFO << "===== [TEST] 创建群组: " << name << " =====";
    chat::CreateGroupRequest req;
    req.set_creator_id(creatorId);
    req.set_group_name(name);
    req.set_group_desc(desc);

    chat::BaseMessage response;
    if (!sendType(sockfd, chat::CREATE_GROUP_MSG, req, response)) return false;
    if (response.type() != chat::CREATE_GROUP_MSG_ACK) return false;

    chat::CreateGroupAck ack;
    ack.ParseFromString(response.payload());
    WT_LOG_INFO << "创建群组响应: code=" << ack.code() << " group_id=" << ack.group_id();
    if (ack.code() != 0) return false;
    outGroupId = ack.group_id();
    return true;
}

// 测试6：加入群组
bool testAddGroup(int sockfd, int userId, int groupId)
{
    WT_LOG_INFO << "===== [TEST] 加入群组: user=" << userId << " group=" << groupId << " =====";
    chat::AddGroupRequest req;
    req.set_user_id(userId);
    req.set_group_id(groupId);

    chat::BaseMessage response;
    if (!sendType(sockfd, chat::ADD_GROUP_MSG, req, response)) return false;
    if (response.type() != chat::ADD_GROUP_MSG_ACK) return false;

    chat::AddGroupAck ack;
    ack.ParseFromString(response.payload());
    WT_LOG_INFO << "加入群组响应: code=" << ack.code() << " msg=" << ack.msg();
    return ack.code() == 0;
}

// 测试7：一对一私聊（离线存储）
// 改造点：生成 msg_id 与 seq，验证服务端去重与离线存储
bool testOneChat(int sockfd, int fromId, int toId, const string &content)
{
    WT_LOG_INFO << "===== [TEST] 私聊: " << fromId << " -> " << toId << " =====";
    int64_t msg_id; int32_t seq;
    nextMsgIdSeq(msg_id, seq);
    chat::OneChatRequest req;
    req.set_from_id(fromId);
    req.set_to_id(toId);
    req.set_content(content);
    req.set_msg_id(msg_id);
    req.set_seq(seq);

    chat::BaseMessage response;
    if (!sendType(sockfd, chat::ONE_CHAT_MSG, req, response)) return false;
    if (response.type() != chat::ONE_CHAT_MSG_ACK) return false;

    chat::OneChatAck ack;
    ack.ParseFromString(response.payload());
    WT_LOG_INFO << "私聊响应: code=" << ack.code() << " msg=" << ack.msg()
                << " msg_id=" << ack.msg_id() << " seq=" << ack.seq();
    return ack.code() == 0;
}

// 测试8：群聊
// 改造点：生成 msg_id 与 seq，验证群聊广播有序投递
bool testGroupChat(int sockfd, int fromId, int groupId, const string &content)
{
    WT_LOG_INFO << "===== [TEST] 群聊: from=" << fromId << " group=" << groupId << " =====";
    int64_t msg_id; int32_t seq;
    nextMsgIdSeq(msg_id, seq);
    chat::GroupChatRequest req;
    req.set_from_id(fromId);
    req.set_group_id(groupId);
    req.set_content(content);
    req.set_msg_id(msg_id);
    req.set_seq(seq);

    chat::BaseMessage response;
    if (!sendType(sockfd, chat::GROUP_CHAT_MSG, req, response)) return false;
    if (response.type() != chat::GROUP_CHAT_MSG_ACK) return false;

    chat::GroupChatAck ack;
    ack.ParseFromString(response.payload());
    WT_LOG_INFO << "群聊响应: code=" << ack.code() << " msg=" << ack.msg()
                << " msg_id=" << ack.msg_id() << " seq=" << ack.seq();
    return ack.code() == 0;
}

// 测试9：注销（loginOut不发送ACK，只需send成功）
bool testLoginOut(int sockfd, int userId)
{
    WT_LOG_INFO << "===== [TEST] 注销: user=" << userId << " =====";
    chat::LoginRequest req;
    req.set_id(userId);

    chat::BaseMessage msg;
    msg.set_type(chat::LOGINOUT_MSG);
    msg.set_payload(req.SerializeAsString());
    bool ok = sendFrame(sockfd, msg);
    WT_LOG_INFO << "注销请求已发送, ok=" << ok;
    return ok;
}

// 测试10：登录后接收离线消息
// 协议已加4字节长度前缀分帧，每条消息独立解析，不再有粘包问题。
bool testReceiveOfflineMsgs(int sockfd, int minExpected)
{
    WT_LOG_INFO << "===== [TEST] 接收离线消息, 期望至少 " << minExpected << " 条 =====";
    // 等待服务端异步投递离线消息（threadpool异步发送，需短暂等待）
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    int received = 0;
    chat::BaseMessage msg;
    while (recvOptional(sockfd, msg, 2))
    {
        received++;
        WT_LOG_INFO << "收到离线消息 type=" << msg.type();
    }
    WT_LOG_INFO << "共收到 " << received << " 条离线消息";
    return received >= minExpected;
}

// ---------- 主函数 ----------

int main(int argc, char *argv[])
{
    // 异步日志初始化
    string path = "/home/wangt/ThreadPoolAction/logmsg/cli";
    if (!fs::exists(path)) fs::create_directories(path);
    asynclog = new wangt::AsyncLogging(path + "/client", 1024 * 10);
    asynclog->start();
    wangt::Logger::setOutput(asyncWriteFile);
    wangt::Logger::setFlush(asyncFlushFile);

    if (argc < 3)
    {
        WT_LOG_ERROR << "Usage: " << argv[0] << " <ip> <port>";
        return 1;
    }
    string ip = argv[1];
    int port = atoi(argv[2]);

    // 连接A（用户A的操作）
    int connA = connectToServer(ip, port);
    if (connA < 0) { WT_LOG_ERROR << "connect A failed"; return -1; }
    WT_LOG_INFO << "Connected A to " << ip << ":" << port;

    string pid = to_string(getpid());
    string userA = "userA_" + pid;
    string userB = "userB_" + pid;
    string pwd = "password123";
    int64_t idA = -1, idB = -1, groupId = -1;

    vector<TestResult> results;

    // 1. 注册用户A和B
    results.push_back({"注册用户A", testRegister(connA, userA, pwd, idA)});
    WT_LOG_INFO << "userA id=" << idA;
    results.push_back({"注册用户B", testRegister(connA, userB, pwd, idB)});
    WT_LOG_INFO << "userB id=" << idB;

    if (idA <= 0 || idB <= 0)
    {
        WT_LOG_ERROR << "注册失败，无法继续测试";
        results.push_back({"后续测试", false});
    }
    else
    {
        // 2. 登录用户A
        results.push_back({"登录用户A", testLogin(connA, userA, pwd, idA)});

        // 3. 添加好友 A->B
        results.push_back({"添加好友", testAddFriend(connA, idA, idB)});

        // 4. 删除好友 A->B
        results.push_back({"删除好友", testDelFriend(connA, idA, idB)});

        // 5. 创建群组
        results.push_back({"创建群组", testCreateGroup(connA, idA, "test_group_" + pid, "test desc", groupId)});
        WT_LOG_INFO << "group id=" << groupId;

        if (groupId > 0)
        {
            // 6. 用户B加入群组
            results.push_back({"加入群组", testAddGroup(connA, idB, groupId)});

            // 7. 私聊 A->B（B离线，应存储离线消息）
            results.push_back({"私聊(离线存储)", testOneChat(connA, idA, idB, "Hello B, this is offline msg")});

            // 8. 群聊 A->group（B离线，应存储离线消息）
            results.push_back({"群聊(离线存储)", testGroupChat(connA, idA, groupId, "Group hello from A")});
        }
        else
        {
            results.push_back({"加入群组", false});
            results.push_back({"私聊(离线存储)", false});
            results.push_back({"群聊(离线存储)", false});
        }

        // 9. 注销用户A
        results.push_back({"注销用户A", testLoginOut(connA, idA)});
    }

    close(connA);

    // 10. 用户B登录，接收离线消息
    if (idB > 0)
    {
        int connB = connectToServer(ip, port);
        if (connB >= 0)
        {
            results.push_back({"登录用户B", testLogin(connB, userB, pwd, idB)});
            // 登录后应收到离线消息（私聊+群聊，但可能合并在一个TCP段中）
            results.push_back({"接收离线消息", testReceiveOfflineMsgs(connB, 1)});
            close(connB);
        }
        else
        {
            results.push_back({"登录用户B", false});
            results.push_back({"接收离线消息", false});
        }
    }

    // 汇总测试结果
    int passCount = 0;
    cout << "\n============================================================" << endl;
    cout << " 测试结果汇总" << endl;
    cout << "============================================================" << endl;
    for (const auto &r : results)
    {
        cout << "  " << (r.passed ? "[PASS]" : "[FAIL]") << "  " << r.name << endl;
        if (r.passed) ++passCount;
    }
    cout << "============================================================" << endl;
    cout << " 通过: " << passCount << "/" << results.size() << endl;
    cout << "============================================================" << endl;

    asynclog->stop();
    delete asynclog;
    return passCount == results.size() ? 0 : 1;
}
