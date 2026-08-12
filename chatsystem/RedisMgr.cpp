#include "RedisMgr.hpp"

RedisMgr *RedisMgr::instance()
{
    static RedisMgr mgr;
    return &mgr;
}

RedisMgr::RedisMgr() : _publishCtx(nullptr), _subscribeCtx(nullptr), _running(false)
{
}

RedisMgr::~RedisMgr()
{
    stopSubscribe();
    if (_publishCtx) redisFree(_publishCtx);
    if (_subscribeCtx) redisFree(_subscribeCtx);
}

bool RedisMgr::connect(const string &ip, int port)
{
    lock_guard<mutex> lock(_mutex);
    if (_publishCtx)
    {
        redisFree(_publishCtx);
        _publishCtx = nullptr;
    }
    struct timeval tv = {2, 0}; // 2秒超时
    _publishCtx = redisConnectWithTimeout(ip.c_str(), port, tv);
    if (!_publishCtx || _publishCtx->err)
    {
        if (_publishCtx)
        {
            redisFree(_publishCtx);
            _publishCtx = nullptr;
        }
        return false;
    }
    return true;
}

redisReply *RedisMgr::executeCommand(const char *format, ...)
{
    lock_guard<mutex> lock(_mutex);
    if (!_publishCtx || _publishCtx->err)
    {
        return nullptr;
    }
    va_list ap;
    va_start(ap, format);
    redisReply *reply = (redisReply *)redisvCommand(_publishCtx, format, ap);
    va_end(ap);
    return reply;
}

bool RedisMgr::publish(const string &channel, const string &message)
{
    redisReply *reply = (redisReply *)executeCommand("PUBLISH %s %b", channel.c_str(), message.c_str(), message.size());
    if (!reply)
    {
        return false;
    }
    bool ok = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);
    return ok;
}

void RedisMgr::startSubscribe(const string &channel, SubscribeCallback cb)
{
    if (_running) return;

    _running = true;

    _subThread = thread([this, channel, cb]() {
        // SUBSCRIBE连接独立于PUBLISH连接，hiredis在SUBSCRIBE模式下阻塞等待消息
        struct timeval tv = {2, 0};
        _subscribeCtx = redisConnectWithTimeout("127.0.0.1", 6379, tv);
        if (!_subscribeCtx || _subscribeCtx->err)
        {
            if (_subscribeCtx)
            {
                redisFree(_subscribeCtx);
                _subscribeCtx = nullptr;
            }
            _running = false;
            return;
        }

        // 发送SUBSCRIBE命令
        redisReply *reply = (redisReply *)redisCommand(_subscribeCtx, "SUBSCRIBE %s", channel.c_str());
        if (reply) freeReplyObject(reply);

        // 阻塞循环接收消息
        while (_running)
        {
            void *r = nullptr;
            int rc = redisGetReply(_subscribeCtx, &r);
            if (rc != REDIS_OK || !r) break;

            redisReply *msgReply = (redisReply *)r;
            // SUBSCRIBE消息格式：["message", channel, data]
            if (msgReply->type == REDIS_REPLY_ARRAY && msgReply->elements >= 3)
            {
                string channelName(msgReply->element[1]->str, msgReply->element[1]->len);
                string data(msgReply->element[2]->str, msgReply->element[2]->len);
                if (cb) cb(data);
            }
            freeReplyObject(msgReply);
        }

        if (_subscribeCtx)
        {
            redisFree(_subscribeCtx);
            _subscribeCtx = nullptr;
        }
        _running = false;
    });
}

void RedisMgr::stopSubscribe()
{
    _running = false;
    // SUBSCRIBE线程在redisGetReply阻塞，关闭连接使其退出
    if (_subscribeCtx)
    {
        // 在_subscribeCtx上发送UNSUBSCRIBE使其退出阻塞
        // 注意：不能在持有_mutex的情况下操作_subscribeCtx（与PUBLISH连接分离）
        redisReply *reply = (redisReply *)redisCommand(_subscribeCtx, "UNSUBSCRIBE");
        if (reply) freeReplyObject(reply);
    }
    if (_subThread.joinable())
    {
        _subThread.join();
    }
}

bool RedisMgr::addUserOnline(int userid)
{
    redisReply *reply = (redisReply *)executeCommand("SADD chat:online_users %d", userid);
    if (!reply) return false;
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 1);
    freeReplyObject(reply);
    return ok;
}

bool RedisMgr::removeUserOnline(int userid)
{
    redisReply *reply = (redisReply *)executeCommand("SREM chat:online_users %d", userid);
    if (!reply) return false;
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 1);
    freeReplyObject(reply);
    return ok;
}

bool RedisMgr::isUserOnline(int userid)
{
    redisReply *reply = (redisReply *)executeCommand("SISMEMBER chat:online_users %d", userid);
    if (!reply) return false;
    bool online = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return online;
}
