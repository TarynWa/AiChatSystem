#include "chatserver.hpp"

void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    if (conn->connected())
    {
        WT_LOG_INFO<<"ChatServer - "<<conn->peerAddress().toIpPort().c_str()<<" connected";
    }
    else
    {
        WT_LOG_INFO<<"ChatServer - "<<conn->peerAddress().toIpPort().c_str()<<" disconnected";
        chatservice::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

void ChatServer::onMessage(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time)
{
    // 消息分帧：每条消息 = [4字节大端长度][protobuf payload]
    // 解决 TCP 粘包：缓冲区可能包含多条消息，循环按长度前缀切分逐条处理
    while (buffer->readableBytes() >= kHeaderLen)
    {
        // peek 4字节长度前缀（不消费）
        int32_t be32 = 0;
        memcpy(&be32, buffer->peek(), kHeaderLen);
        int32_t len = ntohl(be32);
        // 合法性检查
        if (len <= 0 || len > 16 * 1024 * 1024)
        {
            WT_LOG_ERROR << "Invalid frame length: " << len << ", closing connection";
            conn->shutdown();
            return;
        }
        // 数据不足一帧，等待后续到达
        if (static_cast<int32_t>(buffer->readableBytes()) < kHeaderLen + len)
            break;
        // 消费长度前缀 + 取出 protobuf payload
        buffer->retrieve(kHeaderLen);
        string payload = buffer->retrieveAsString(len);
        chatservice::instance()->recvmsg(conn, payload, time);
    }
}

ChatServer::ChatServer(EventLoop *loop, const InetAddress &listenAddr, const string &nameArg) : loop_(loop), server_(loop, listenAddr, nameArg)
{
    WT_LOG_INFO << "ChatServer constructor called";
    server_.setConnectionCallback(
        std::bind(&ChatServer::onConnection, this, _1));
    server_.setMessageCallback(
        std::bind(&ChatServer::onMessage, this, _1, _2, _3));
    // server_.setThreadNum(4);
    //threadpool_.start(4);
}

// void ChatServer::doHeavyBusiness(const TcpConnectionPtr &conn, const string &msg)
// {
//     try{
//         if (conn->connected()) {
//             conn->getLoop()->runInLoop([conn, msg]() {
//                 conn->send(msg);
//             });
//         }
//     }catch(const exception& e){
//         LOG_ERROR << "Biz thread exception: " << e.what();
//         // 异常处理：通知客户端错误、清理资源等
//         if (conn->connected()) {
//             conn->getLoop()->runInLoop([conn]() {
//                 conn->send("Server internal error!");
//             });
//     }
//     }
// }

void ChatServer::start()
{
    server_.start();
}