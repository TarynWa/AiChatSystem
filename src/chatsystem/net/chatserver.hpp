#ifndef CHATSERVER_HPP
#define CHATSERVER_HPP
#include<functional>
#include<string>
#include<iostream>
#include<cstring>
#include<arpa/inet.h>
#include"net/nwl_bridge.hpp"   // 自研网络库（nwl）类型桥接，平替 muduo::net
#include"service/chatservice.hpp"
#include"AsyncLogging.hpp"
#include"proto/chat.pb.h"
using namespace std;
using namespace placeholders;
class ChatServer
{
    static constexpr int kHeaderLen = 4; // 消息长度前缀字节数
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