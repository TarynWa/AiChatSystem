#ifndef CHATSERVICE_H
#define CHATSERVICE_H
#include<string>
//#include"chatserver.hpp"
#include<unordered_map>
#include<functional>
#include<mutex>
#include<memory>
#include<muduo/base/ThreadPool.h> // 异步 DB 工作线程池（与网络层无关，保留）
#include<arpa/inet.h>
#include"net/chatserver.hpp"      // 经由 nwl_bridge.hpp 引入自研网络库类型
#include"proto/chat.pb.h"
#include"model/UserModel.hpp"
#include"model/FriendModel.hpp"
#include"model/GroupModel.hpp"
#include"model/OfflineMsgModel.hpp"
#include"storage/RedisMgr.hpp"
#include"util/PwdUtils.hpp"
using namespace std;
using MsgHandler=function<void(const TcpConnectionPtr& conn,const string& str, Timestamp time)>;
class chatservice
{
    public:
    static chatservice* instance();
    // 发送带4字节长度前缀的消息帧，解决TCP粘包
    static void sendFrame(const TcpConnectionPtr& conn, const string& payload);
    void recvmsg(const TcpConnectionPtr& conn,const string& js, Timestamp time);
    //处理客户端异常退出（连接断开时清理在线状态）
    void clientCloseException(const TcpConnectionPtr& conn);
    //处理登录业务
    void login(const TcpConnectionPtr& conn,const string& js, Timestamp time);
    //处理注册业务（异步执行DB操作，不阻塞muduo网络IO线程）
    void reg(const TcpConnectionPtr& conn, const string& js, Timestamp time);
    // ========== 以下为后续业务handler接口 ==========
    //一对一私聊业务
    void oneChat(const TcpConnectionPtr& conn, const string& js, Timestamp time);
    //添加好友业务
    void addFriend(const TcpConnectionPtr& conn, const string& js, Timestamp time);
    //删除好友业务
    void delFriend(const TcpConnectionPtr& conn, const string& js, Timestamp time);
    //创建群组业务
    void createGroup(const TcpConnectionPtr& conn, const string& js, Timestamp time);
    //加入群组业务
    void addGroup(const TcpConnectionPtr& conn, const string& js, Timestamp time);
    //退出群组业务
    void quitGroup(const TcpConnectionPtr& conn, const string& js, Timestamp time);
    //群聊天业务
    void groupChat(const TcpConnectionPtr& conn, const string& js, Timestamp time);
    // 注销业务
    void loginOut(const TcpConnectionPtr& conn, const string& js, Timestamp time);
    // 初始化Redis连接并启动SUBSCRIBE线程
    void initRedis();

    private:
    // static unique_ptr<chatservice> service= make_unique<chatservice>();
    chatservice(){
        // 初始化消息id和对应的处理器
        threadpool_.start(4);
        _msgHandlerMap.insert({chat::EnMsgType::LOGIN_MSG, std::bind(&chatservice::login, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
        _msgHandlerMap.insert({chat::EnMsgType::REG_MSG, std::bind(&chatservice::reg, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
        _msgHandlerMap.insert({chat::EnMsgType::ONE_CHAT_MSG, std::bind(&chatservice::oneChat, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
        _msgHandlerMap.insert({chat::EnMsgType::ADD_FRIEND_MSG, std::bind(&chatservice::addFriend, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
        _msgHandlerMap.insert({chat::EnMsgType::DEL_FRIEND_MSG, std::bind(&chatservice::delFriend, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
        _msgHandlerMap.insert({chat::EnMsgType::CREATE_GROUP_MSG, std::bind(&chatservice::createGroup, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
        _msgHandlerMap.insert({chat::EnMsgType::ADD_GROUP_MSG, std::bind(&chatservice::addGroup, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
        _msgHandlerMap.insert({chat::EnMsgType::QUIT_GROUP_MSG, std::bind(&chatservice::quitGroup, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
        _msgHandlerMap.insert({chat::EnMsgType::GROUP_CHAT_MSG, std::bind(&chatservice::groupChat, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
        _msgHandlerMap.insert({chat::EnMsgType::LOGINOUT_MSG, std::bind(&chatservice::loginOut, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
    }
    chatservice(const chatservice&)=delete;
    chatservice& operator=(const chatservice&)=delete;
    mutex m_mutex;
     // 存储消息id和其对应的业务处理方法
    unordered_map<chat::EnMsgType, MsgHandler> _msgHandlerMap;
    // 存储在线用户的连接（userid -> TcpConnection），用于私聊/群聊消息转发
    unordered_map<int, TcpConnectionPtr> _userConnMap;
    std::shared_ptr<UserModel> _userModel = std::make_shared<UserModel>();
    std::shared_ptr<FriendModel> _friendModel = std::make_shared<FriendModel>();
    std::shared_ptr<GroupModel> _groupModel = std::make_shared<GroupModel>();
    std::shared_ptr<OfflineMsgModel> _offlineMsgModel = std::make_shared<OfflineMsgModel>();
    muduo::ThreadPool threadpool_;

};
#endif