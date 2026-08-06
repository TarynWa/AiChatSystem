#ifndef CHATSERVICE_H
#define CHATSERVICE_H
#include<string>
//#include"chatserver.hpp"
#include<unordered_map>
#include<muduo/net/TcpConnection.h>//muduo库中Tcp连接类的头文件
#include<functional>
#include<mutex>
#include<memory>
#include<muduo/base/Logging.h>
#include<muduo/base/ThreadPool.h>
#include"chatserver.hpp"
#include"chat.pb.h"
using namespace muduo;
using namespace muduo::net;
using namespace std; 
using MsgHandler=function<void(const TcpConnectionPtr& conn,const string& str, Timestamp time)>;
class chatservice
{
    public:
    static chatservice* instance();
    void recvmsg(const TcpConnectionPtr& conn,const string& js, Timestamp time);
    //获取消息对应的处理器
    // MsgHandler getMsgHandler(int msgid);
    // void  sendResponse(const TcpConnectionPtr& conn, const json& js); 
    // //处理客户端异常退出
    // void clientCloseException(const TcpConnectionPtr& conn);
    // //处理登录业务
    void login(const TcpConnectionPtr& conn,const string& js, Timestamp time);
    // //处理注册业务
    // void reg(const TcpConnectionPtr& conn, json& js, Timestamp time);
    // //一对一聊天业务
    // void oneChat(const TcpConnectionPtr& conn, json& js, Timestamp time);
    // //添加好友业务
    // void addFriend(const TcpConnectionPtr& conn, json& js, Timestamp time);
    // //创建群组业务
    // void createGroup(const TcpConnectionPtr& conn, json& js, Timestamp time);
    // //加入群组业务
    // void addGroup(const TcpConnectionPtr& conn, json& js, Timestamp time);
    // //群聊天业务
    // void groupChat(const TcpConnectionPtr& conn, json& js, Timestamp time);
    // // 注销业务
    // void loginOut(const TcpConnectionPtr& conn, json& js, Timestamp time);
    // // 从redis消息队列中获取订阅的消息
    // void handleRedisSubscribeMessage(int, string);

    private:
    // static unique_ptr<chatservice> service= make_unique<chatservice>();
    chatservice(){
        // 初始化消息id和对应的处理器
        threadpool_.start(4);
        _msgHandlerMap.insert({chat::EnMsgType::LOGIN_MSG, std::bind(&chatservice::login, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
    }
    chatservice(const chatservice&)=delete;
    chatservice& operator=(const chatservice&)=delete;
    mutex m_mutex;
     // 存储消息id和其对应的业务处理方法
    unordered_map<chat::EnMsgType, MsgHandler> _msgHandlerMap;
    // unordered_map<int, TcpConnectionPtr> _userConnMap;
    ThreadPool threadpool_;
};
#endif