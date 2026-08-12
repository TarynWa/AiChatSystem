#include "chatservice.hpp"

chatservice *chatservice::instance()
{
    static chatservice service;
    return &service;
}

void chatservice::recvmsg(const TcpConnectionPtr &conn, const string &js, Timestamp time)
{
    chat::BaseMessage menu;
    bool success = menu.ParseFromString(js);
    if(!success){
        WT_LOG_ERROR << "Deserialization failed!";
        return ;
    }
    chat::EnMsgType tp = menu.type();
    if(_msgHandlerMap.count(tp)){
    WT_LOG_INFO<<"MENU is exit";
    auto msgHandler = _msgHandlerMap[tp];
    msgHandler(conn,menu.payload(),time);
    }else{
        WT_LOG_ERROR<<"MENU TYPE IS NULL";
    }
}


void chatservice::login(const TcpConnectionPtr &conn,const string &js, Timestamp time)
{
    cout<<"HELLO LOGIN";
}
