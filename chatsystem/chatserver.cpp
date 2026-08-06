#include "chatserver.hpp"

void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    if (conn->connected())
    {
        printf("ChatServer - %s connected\n", conn->peerAddress().toIpPort().c_str());
    }
    else
    {
        printf("ChatServer - %s disconnected\n", conn->peerAddress().toIpPort().c_str());
        // chatservice::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

void ChatServer::onMessage(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time)
{
    string buff = buffer->retrieveAllAsString();
    // printf("ChatServer received message:%s\n", buff.c_str());
    // conn->send(buff);
    // threadpool_.run(std::bind(&ChatServer::doHeavyBusiness,this,conn,buff));
    // 业务处理模块
    // try{
    // json js = json::parse(buff);
    // chatservice::instance()->recvmsg(conn, js, time);
    // }catch(const exception& e){
    //     //
    //     LOG_ERROR << "JSON parse error: " << e.what();
    // }
    //int id = js["msgid"].get<int>();
    // auto msgHandler=chatservice::instance()->getMsgHandler(id);
    // msgHandler(conn,js,time);
    // auto chatservice = chatservice::instance();
    // auto msgHandler = chatservice->getMsgHandler(id);
    // threadpool_.run(std::bind(msgHandler, conn, js, time));
   // threadpool_.run(std::bind((chatservice::instance()->getMsgHandler(id)),conn,js,time));

}

ChatServer::ChatServer(EventLoop *loop, const InetAddress &listenAddr, const string &nameArg) : loop_(loop), server_(loop, listenAddr, nameArg)
{
    server_.setConnectionCallback(
        std::bind(&ChatServer::onConnection, this, _1));
    server_.setMessageCallback(
        std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    server_.setThreadNum(4);
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