#include "chatservice.hpp"

chatservice *chatservice::instance()
{
    static chatservice service;
    return &service;
}

void chatservice::recvmsg(const TcpConnectionPtr &conn, chat::BaseMessage &js, Timestamp time)
{
}

void chatservice::login(const TcpConnectionPtr &conn, string &js, Timestamp time)
{
}
