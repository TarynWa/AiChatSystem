#ifndef REDISMGR_H
#define REDISMGR_H

#include <string>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <hiredis/hiredis.h>

using namespace std;

// Redis管理类（单例）
// 职责：
// 1. PUBLISH：向Redis频道发布跨节点消息
// 2. SUBSCRIBE：独立线程阻塞订阅频道，收到消息后回调通知上层
// 3. 在线用户集合：维护全局在线用户SET（SADD/SREM/SISMEMBER），用于判断用户是否在任意节点在线
class RedisMgr
{
public:
    // 获取订阅消息的回调类型
    using SubscribeCallback = function<void(const string &)>;

    static RedisMgr *instance();

    // 连接Redis（初始化PUBLISH连接）
    bool connect(const string &ip = "127.0.0.1", int port = 6379);

    // PUBLISH：向指定频道发布消息
    bool publish(const string &channel, const string &message);

    // 启动SUBSCRIBE线程，阻塞订阅指定频道
    void startSubscribe(const string &channel, SubscribeCallback cb);

    // 停止SUBSCRIBE线程
    void stopSubscribe();

    // ========== 全局在线用户SET操作 ==========
    // 用户上线时调用
    bool addUserOnline(int userid);
    // 用户下线时调用
    bool removeUserOnline(int userid);
    // 检查用户是否在任意节点在线
    bool isUserOnline(int userid);

    // ========== 消息幂等去重窗 ==========
    // 尝试为 (from_id, msg_id) 抢占去重标记（SETNX）
    // 返回 true 表示首次获得标记，应正常处理
    // 返回 false 表示已存在（重复请求），调用方应直接返回缓存的 ACK
    bool tryAcquireDedup(int64_t from_id, int64_t msg_id, int ttl_seconds = 600);
    // 缓存 ACK payload，供重传命中时返回（避免重复执行业务）
    bool cacheAck(int64_t from_id, int64_t msg_id, const string &ack_payload, int ttl_seconds = 600);
    // 读取缓存的 ACK payload；返回 false 表示未命中或已过期
    bool getCachedAck(int64_t from_id, int64_t msg_id, string &out_ack_payload);

private:
    RedisMgr();
    ~RedisMgr();
    RedisMgr(const RedisMgr &) = delete;
    RedisMgr &operator=(const RedisMgr &) = delete;

    // 执行Redis命令的辅助函数（加锁保护_publishCtx）
    redisReply *executeCommand(const char *format, ...);

    redisContext *_publishCtx;   // PUBLISH/SET操作的连接（线程安全靠_mutex）
    redisContext *_subscribeCtx; // SUBSCRIBE专用连接（独立线程使用）
    mutex _mutex;               // 保护_publishCtx的并发访问
    atomic<bool> _running;       // SUBSCRIBE线程运行标志
    thread _subThread;           // SUBSCRIBE线程
};

#endif
