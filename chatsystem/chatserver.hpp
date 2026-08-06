#ifndef CHATSERVER_HPP
#define CHATSERVER_HPP
#include<muduo/net/TcpServer.h>
#include<muduo/net/EventLoop.h>
#include<muduo/base/ThreadPool.h>
#include<muduo/base/Logging.h>
#include<functional>
#include<string>
#include<iostream>
// #include"chatservice.hpp"
using namespace muduo;
using namespace muduo::net;
using namespace std;
using namespace placeholders;   
class ChatServer
{
    TcpServer server_;
    EventLoop* loop_;
    //ThreadPool threadpool_;
    //上报连接相关信息的回调函数
    void onConnection(const TcpConnectionPtr&);
    //上报读写事件相关信息的回调函数
    void onMessage(const TcpConnectionPtr&,Buffer*,Timestamp);
public:
    ChatServer(EventLoop*loop, const InetAddress& listenAddr,const string& nameArg);
   /// void doHeavyBusiness(const TcpConnectionPtr& conn, const string& msg);
    void start();

};
#endif