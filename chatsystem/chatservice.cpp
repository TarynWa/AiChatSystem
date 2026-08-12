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
       WT_LOG_INFO<<"chatservice::login() begin";
       chat::LoginRequest loginRequest;
       bool success = loginRequest.ParseFromString(js);
       if(!success){
           WT_LOG_ERROR << "Deserialization failed!";
           return ;
       }
       string name = loginRequest.username();
       string password = loginRequest.password();
       int id = loginRequest.id();
       WT_LOG_INFO<<"username:"<<name<<" password:"<<password;
       User _user = _userModel->query(id);
       if(_user.getPwd() == password && _user.getName() == name){
           WT_LOG_INFO<<"login success";
           _user.setState("online");
           _userModel->updateState(_user);
           chat::BaseMessage response;
           response.set_type(chat::LOGIN_MSG_ACK);
           chat::LoginRequest loginResponse;
           loginResponse.set_username(name);
           loginResponse.set_password(password);
           loginResponse.set_id(id);
           response.set_payload(loginResponse.SerializeAsString());
           string buf;
           if (!response.SerializeToString(&buf))
           {
               WT_LOG_ERROR << "Serialization failed!";
               return ;
           }
           conn->send(buf);
       }else{
           WT_LOG_ERROR<<"login failed";
           chat::BaseMessage response;
           response.set_type(chat::LOGIN_MSG_ACK);
           chat::LoginRequest loginResponse;
           loginResponse.set_username(name);
           loginResponse.set_password(password);
           loginResponse.set_id(-1);
           response.set_payload(loginResponse.SerializeAsString());
           string buf;
           if (!response.SerializeToString(&buf))
           {
               WT_LOG_ERROR << "Serialization failed!";
               return ; 
           }
              conn->send(buf);
        }
}

void chatservice::reg(const TcpConnectionPtr &conn, const string &js, Timestamp time)
{
    WT_LOG_INFO << "chatservice::reg() begin";
    chat::RegisterRequest registerRequest;
    bool success = registerRequest.ParseFromString(js);
    if (!success)
    {
        WT_LOG_ERROR << "RegisterRequest deserialization failed!";
        return;
    }

    string name = registerRequest.username();
    string password = registerRequest.password();
    int id = registerRequest.id();
    chat::BaseMessage response;
    response.set_type(chat::REG_MSG_ACK);
    chat::RegisterResponse registerResponse;

    if (name.empty() || id==-1 || password.empty())
    {
        registerResponse.set_code(1);
        registerResponse.set_msg("Invalid username or password.");
        response.set_payload(registerResponse.SerializeAsString());
        string buf;
        response.SerializeToString(&buf);
        conn->send(buf);
        return;
    }

    User existingUser = _userModel->query(id);
    if (existingUser.getId() != 0)
    {
        registerResponse.set_code(2);
        registerResponse.set_msg("Username already exists.");
        response.set_payload(registerResponse.SerializeAsString());
        string buf;
        response.SerializeToString(&buf);
        conn->send(buf);
        return;
    }

    User newUser;
    newUser.setName(name);
    newUser.setPwd(password);
    newUser.setState("offline");

    if (!_userModel->insert(newUser))
    {
        registerResponse.set_code(3);
        registerResponse.set_msg("Registration failed, please try again.");
    }
    else
    {
        registerResponse.set_code(0);
        registerResponse.set_msg("Registration successful.");
    }

    response.set_payload(registerResponse.SerializeAsString());
    string buf;
    response.SerializeToString(&buf);
    conn->send(buf);
}