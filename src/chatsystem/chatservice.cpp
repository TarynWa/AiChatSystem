#include "chatservice.hpp"

chatservice *chatservice::instance()
{
    static chatservice service;
    return &service;
}

// 发送带4字节大端长度前缀的消息帧：[4字节长度][protobuf payload]
// 与 onMessage 的分帧逻辑配对，彻底解决TCP粘包导致的多消息丢弃问题
void chatservice::sendFrame(const TcpConnectionPtr& conn, const string& payload)
{
    if (!conn || !conn->connected())
        return;
    int32_t be32 = htonl(static_cast<int32_t>(payload.size()));
    string frame;
    frame.reserve(sizeof(be32) + payload.size());
    frame.append(reinterpret_cast<const char*>(&be32), sizeof(be32));
    frame.append(payload);
    conn->send(frame);
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
       // 密码校验：使用PwdUtils对客户端明文密码加盐哈希后与DB中存储的哈希值比较
       if(PwdUtils::verify(password, _user.getSalt(), _user.getPwd()) && _user.getName() == name){
           WT_LOG_INFO<<"login success";
           _user.setState("online");
           _userModel->updateState(_user);

           // 登录成功后记录在线连接，用于私聊/群聊消息转发
           {
               lock_guard<mutex> lock(m_mutex);
               _userConnMap[id] = conn;
           }
           // 同步至Redis全局在线用户SET（集群多节点共享）
           RedisMgr::instance()->addUserOnline(id);

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
           chatservice::sendFrame(conn, buf);

           // 异步拉取并投递离线消息（不阻塞IO线程）
           int userId = id;
           TcpConnectionPtr connPtr = conn;
           threadpool_.run([this, userId, connPtr]() {
               vector<OfflineMsg> offlineMsgs = _offlineMsgModel->query(userId);
               for (const auto &msg : offlineMsgs)
               {
                   chat::BaseMessage fwd;
                   if (msg.getMsgType() == "private")
                   {
                       fwd.set_type(chat::ONE_CHAT_MSG);
                       chat::OneChatRequest chatReq;
                       chatReq.set_from_id(msg.getFromId());
                       chatReq.set_to_id(userId);
                       chatReq.set_content(msg.getContent());
                       fwd.set_payload(chatReq.SerializeAsString());
                   }
                   else // group
                   {
                       fwd.set_type(chat::GROUP_CHAT_MSG);
                       chat::GroupChatRequest groupReq;
                       groupReq.set_from_id(msg.getFromId());
                       groupReq.set_group_id(0); // 离线群消息已不携带原群ID
                       groupReq.set_content(msg.getContent());
                       fwd.set_payload(groupReq.SerializeAsString());
                   }
                   string buf;
                   fwd.SerializeToString(&buf);
                   chatservice::sendFrame(connPtr, buf);
               }
               if (!offlineMsgs.empty())
               {
                   _offlineMsgModel->remove(userId);
                   WT_LOG_INFO << "Delivered " << offlineMsgs.size() << " offline messages to user " << userId;
               }
           });
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
              chatservice::sendFrame(conn, buf);
        }
}

// 注册业务（异步执行，所有DB操作派发到threadpool_，避免阻塞muduo网络IO线程）
void chatservice::reg(const TcpConnectionPtr &conn, const string &js, Timestamp time)
{
    WT_LOG_INFO << "chatservice::reg() begin";
    chat::RegisterRequest registerRequest;
    if (!registerRequest.ParseFromString(js))
    {
        WT_LOG_ERROR << "RegisterRequest deserialization failed!";
        return;
    }

    string name = registerRequest.username();
    string password = registerRequest.password();

    // 参数合法性校验（不涉及DB，可在IO线程同步完成）
    if (name.empty() || password.empty() || name.size() > 64 || password.size() > 128)
    {
        WT_LOG_ERROR << "Invalid username or password";
        chat::BaseMessage response;
        response.set_type(chat::REG_MSG_ACK);
        chat::RegisterResponse registerResponse;
        registerResponse.set_code(1);
        registerResponse.set_msg("Invalid username or password.");
        registerResponse.set_id(-1);
        response.set_payload(registerResponse.SerializeAsString());
        string buf;
        response.SerializeToString(&buf);
        chatservice::sendFrame(conn, buf);
        return;
    }

    // 异步派发到线程池执行DB操作
    EventLoop *loop = conn->getLoop();
    threadpool_.run([this, conn, loop, name, password]() {
        chat::BaseMessage response;
        response.set_type(chat::REG_MSG_ACK);
        chat::RegisterResponse registerResponse;

        User existingUser = _userModel->queryByName(name);
        if (existingUser.getId() != -1 && existingUser.getId() != 0)
        {
            registerResponse.set_code(2);
            registerResponse.set_msg("Username already exists.");
            registerResponse.set_id(-1);
        }
        else
        {
            string salt = PwdUtils::generateSalt();
            string hashedPwd = PwdUtils::sha256(salt + password);

            User newUser;
            newUser.setName(name);
            newUser.setPwd(hashedPwd);
            newUser.setSalt(salt);
            newUser.setState("offline");

            if (!_userModel->insert(newUser))
            {
                registerResponse.set_code(3);
                registerResponse.set_msg("Registration failed, please try again.");
                registerResponse.set_id(-1);
            }
            else
            {
                registerResponse.set_code(0);
                registerResponse.set_msg("Registration successful.");
                registerResponse.set_id(newUser.getId());
            }
        }

        response.set_payload(registerResponse.SerializeAsString());
        string buf;
        response.SerializeToString(&buf);
        loop->runInLoop([conn, buf]() {
            chatservice::sendFrame(conn, buf);
        });
    });
}

// 客户端异常退出：连接断开时从_userConnMap移除并更新DB状态为offline
void chatservice::clientCloseException(const TcpConnectionPtr &conn)
{
    int userId = -1;
    {
        lock_guard<mutex> lock(m_mutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                userId = it->first;
                _userConnMap.erase(it);
                break;
            }
        }
    }
    if (userId != -1)
    {
        WT_LOG_INFO << "clientCloseException: user " << userId << " disconnected, set offline";
        User user;
        user.setId(userId);
        user.setState("offline");
        _userModel->updateState(user);
        // 从Redis全局在线用户SET中移除
        RedisMgr::instance()->removeUserOnline(userId);
    }
}

// 一对一私聊：本节点在线→直接转发；其他节点在线→Redis PUBLISH；全离线→MySQL存储
void chatservice::oneChat(const TcpConnectionPtr &conn, const string &js, Timestamp time)
{
    WT_LOG_INFO << "chatservice::oneChat() begin";
    chat::OneChatRequest req;
    if (!req.ParseFromString(js))
    {
        WT_LOG_ERROR << "OneChatRequest deserialization failed!";
        return;
    }
    int fromId = req.from_id();
    int toId = req.to_id();
    string content = req.content();

    // 1. 查找接收方是否在本节点在线
    TcpConnectionPtr toConn;
    {
        lock_guard<mutex> lock(m_mutex);
        auto it = _userConnMap.find(toId);
        if (it != _userConnMap.end() && it->second->connected())
        {
            toConn = it->second;
        }
    }

    if (toConn)
    {
        // 本节点在线：直接转发
        chat::BaseMessage fwd;
        fwd.set_type(chat::ONE_CHAT_MSG);
        fwd.set_payload(req.SerializeAsString());
        string fwdBuf;
        fwd.SerializeToString(&fwdBuf);
        chatservice::sendFrame(toConn, fwdBuf);

        chat::BaseMessage response;
        response.set_type(chat::ONE_CHAT_MSG_ACK);
        chat::OneChatAck ack;
        ack.set_code(0);
        ack.set_msg("Message delivered.");
        response.set_payload(ack.SerializeAsString());
        string buf;
        response.SerializeToString(&buf);
        chatservice::sendFrame(conn, buf);
        return;
    }

    // 2. 本节点未命中，异步查Redis全局在线SET + PUBLISH或存储离线
    threadpool_.run([this, conn, toId, fromId, content, req]() {
        if (RedisMgr::instance()->isUserOnline(toId))
        {
            // 用户在其他节点在线：PUBLISH跨节点消息
            chat::CrossNodeMsg crossMsg;
            crossMsg.set_target_user_id(toId);
            crossMsg.set_msg_type(chat::ONE_CHAT_MSG);
            crossMsg.set_payload(req.SerializeAsString());
            string crossData = crossMsg.SerializeAsString();
            RedisMgr::instance()->publish("chat:cross_node", crossData);

            chat::BaseMessage response;
            response.set_type(chat::ONE_CHAT_MSG_ACK);
            chat::OneChatAck ack;
            ack.set_code(0);
            ack.set_msg("Message forwarded to target node.");
            response.set_payload(ack.SerializeAsString());
            string buf;
            response.SerializeToString(&buf);
            chatservice::sendFrame(conn, buf);
        }
        else
        {
            // 用户全离线：存储离线消息到MySQL
            _offlineMsgModel->insert(toId, fromId, "private", content);
            chat::BaseMessage response;
            response.set_type(chat::ONE_CHAT_MSG_ACK);
            chat::OneChatAck ack;
            ack.set_code(0);
            ack.set_msg("Recipient offline, message stored.");
            response.set_payload(ack.SerializeAsString());
            string buf;
            response.SerializeToString(&buf);
            chatservice::sendFrame(conn, buf);
        }
    });
}

// 添加好友（异步DB操作）
void chatservice::addFriend(const TcpConnectionPtr &conn, const string &js, Timestamp time)
{
    WT_LOG_INFO << "chatservice::addFriend() begin";
    chat::AddFriendRequest req;
    if (!req.ParseFromString(js))
    {
        WT_LOG_ERROR << "AddFriendRequest deserialization failed!";
        return;
    }
    int fromId = req.from_id();
    int toId = req.to_id();

    threadpool_.run([this, conn, fromId, toId]() {
        chat::BaseMessage response;
        response.set_type(chat::ADD_FRIEND_MSG_ACK);
        chat::AddFriendAck ack;

        User toUser = _userModel->query(toId);
        if (toUser.getId() == -1 || toUser.getId() == 0)
        {
            ack.set_code(1);
            ack.set_msg("Target user does not exist.");
        }
        else if (_friendModel->insert(fromId, toId))
        {
            ack.set_code(0);
            ack.set_msg("Friend added.");
        }
        else
        {
            ack.set_code(2);
            ack.set_msg("Add friend failed, maybe already friends.");
        }

        response.set_payload(ack.SerializeAsString());
        string buf;
        response.SerializeToString(&buf);
        chatservice::sendFrame(conn, buf);
    });
}

// 删除好友（异步DB操作）
void chatservice::delFriend(const TcpConnectionPtr &conn, const string &js, Timestamp time)
{
    WT_LOG_INFO << "chatservice::delFriend() begin";
    chat::DelFriendRequest req;
    if (!req.ParseFromString(js))
    {
        WT_LOG_ERROR << "DelFriendRequest deserialization failed!";
        return;
    }
    int fromId = req.from_id();
    int toId = req.to_id();

    threadpool_.run([this, conn, fromId, toId]() {
        chat::BaseMessage response;
        response.set_type(chat::DEL_FRIEND_MSG_ACK);
        chat::DelFriendAck ack;

        if (_friendModel->remove(fromId, toId))
        {
            ack.set_code(0);
            ack.set_msg("Friend removed.");
        }
        else
        {
            ack.set_code(1);
            ack.set_msg("Remove friend failed.");
        }

        response.set_payload(ack.SerializeAsString());
        string buf;
        response.SerializeToString(&buf);
        chatservice::sendFrame(conn, buf);
    });
}

// 创建群组（异步DB操作）
void chatservice::createGroup(const TcpConnectionPtr &conn, const string &js, Timestamp time)
{
    WT_LOG_INFO << "chatservice::createGroup() begin";
    chat::CreateGroupRequest req;
    if (!req.ParseFromString(js))
    {
        WT_LOG_ERROR << "CreateGroupRequest deserialization failed!";
        return;
    }

    Group group;
    group.setCreatorId(req.creator_id());
    group.setName(req.group_name());
    group.setDesc(req.group_desc());

    threadpool_.run([this, conn, group]() {
        chat::BaseMessage response;
        response.set_type(chat::CREATE_GROUP_MSG_ACK);
        chat::CreateGroupAck ack;

        Group g = group;
        if (_groupModel->createGroup(g))
        {
            ack.set_code(0);
            ack.set_msg("Group created.");
            ack.set_group_id(g.getId());
        }
        else
        {
            ack.set_code(1);
            ack.set_msg("Create group failed.");
            ack.set_group_id(-1);
        }

        response.set_payload(ack.SerializeAsString());
        string buf;
        response.SerializeToString(&buf);
        chatservice::sendFrame(conn, buf);
    });
}

// 加入群组（异步DB操作）
void chatservice::addGroup(const TcpConnectionPtr &conn, const string &js, Timestamp time)
{
    WT_LOG_INFO << "chatservice::addGroup() begin";
    chat::AddGroupRequest req;
    if (!req.ParseFromString(js))
    {
        WT_LOG_ERROR << "AddGroupRequest deserialization failed!";
        return;
    }
    int userId = req.user_id();
    int groupId = req.group_id();

    threadpool_.run([this, conn, userId, groupId]() {
        chat::BaseMessage response;
        response.set_type(chat::ADD_GROUP_MSG_ACK);
        chat::AddGroupAck ack;

        vector<Group> groups = _groupModel->queryGroups(userId);
        bool alreadyIn = false;
        for (const auto &g : groups)
        {
            if (g.getId() == groupId)
            {
                alreadyIn = true;
                break;
            }
        }
        if (alreadyIn)
        {
            ack.set_code(2);
            ack.set_msg("Already a member of this group.");
        }
        else if (_groupModel->joinGroup(userId, groupId, "normal"))
        {
            ack.set_code(0);
            ack.set_msg("Joined group.");
        }
        else
        {
            ack.set_code(1);
            ack.set_msg("Join group failed, group may not exist.");
        }

        response.set_payload(ack.SerializeAsString());
        string buf;
        response.SerializeToString(&buf);
        chatservice::sendFrame(conn, buf);
    });
}

// 退出群组（异步DB操作）
void chatservice::quitGroup(const TcpConnectionPtr &conn, const string &js, Timestamp time)
{
    WT_LOG_INFO << "chatservice::quitGroup() begin";
    chat::QuitGroupRequest req;
    if (!req.ParseFromString(js))
    {
        WT_LOG_ERROR << "QuitGroupRequest deserialization failed!";
        return;
    }
    int userId = req.user_id();
    int groupId = req.group_id();

    threadpool_.run([this, conn, userId, groupId]() {
        chat::BaseMessage response;
        response.set_type(chat::QUIT_GROUP_MSG_ACK);
        chat::QuitGroupAck ack;

        if (_groupModel->quitGroup(userId, groupId))
        {
            ack.set_code(0);
            ack.set_msg("Quit group.");
        }
        else
        {
            ack.set_code(1);
            ack.set_msg("Quit group failed.");
        }

        response.set_payload(ack.SerializeAsString());
        string buf;
        response.SerializeToString(&buf);
        chatservice::sendFrame(conn, buf);
    });
}

// 群聊：查询群成员→本节点在线转发/其他节点在线Redis PUBLISH/全离线MySQL存储
void chatservice::groupChat(const TcpConnectionPtr &conn, const string &js, Timestamp time)
{
    WT_LOG_INFO << "chatservice::groupChat() begin";
    chat::GroupChatRequest req;
    if (!req.ParseFromString(js))
    {
        WT_LOG_ERROR << "GroupChatRequest deserialization failed!";
        return;
    }
    int fromId = req.from_id();
    int groupId = req.group_id();
    string content = req.content();

    // 异步查询群成员并分发
    threadpool_.run([this, conn, fromId, groupId, content, req]() {
        vector<GroupUser> members = _groupModel->queryGroupMembers(groupId);
        if (members.empty())
        {
            chat::BaseMessage response;
            response.set_type(chat::GROUP_CHAT_MSG_ACK);
            chat::GroupChatAck ack;
            ack.set_code(1);
            ack.set_msg("Group not found or no members.");
            response.set_payload(ack.SerializeAsString());
            string buf;
            response.SerializeToString(&buf);
            chatservice::sendFrame(conn, buf);
            return;
        }

        // 构造转发消息（原始GroupChatRequest）
        string reqPayload = req.SerializeAsString();
        chat::BaseMessage fwd;
        fwd.set_type(chat::GROUP_CHAT_MSG);
        fwd.set_payload(reqPayload);
        string fwdBuf;
        fwd.SerializeToString(&fwdBuf);

        // 分类成员：本节点在线 / 其他节点在线 / 全离线
        vector<TcpConnectionPtr> localOnlineConns;
        vector<int> crossNodeUserIds;  // 需要PUBLISH到Redis的用户
        vector<int> offlineUserIds;    // 需要存储离线消息的用户

        {
            lock_guard<mutex> lock(m_mutex);
            for (const auto &m : members)
            {
                if (m.getId() == fromId) continue; // 不回发给发送者
                auto it = _userConnMap.find(m.getId());
                if (it != _userConnMap.end() && it->second->connected())
                {
                    localOnlineConns.push_back(it->second);
                }
                else
                {
                    // 本节点未命中，稍后查Redis判断是跨节点在线还是全离线
                    crossNodeUserIds.push_back(m.getId());
                }
            }
        }

        // 转发给本节点在线成员
        for (const auto &c : localOnlineConns)
        {
            chatservice::sendFrame(c, fwdBuf);
        }

        // 对本节点未命中的成员，查Redis全局在线SET决定PUBLISH或存储离线
        for (int uid : crossNodeUserIds)
        {
            if (RedisMgr::instance()->isUserOnline(uid))
            {
                // 用户在其他节点在线：PUBLISH跨节点消息
                chat::CrossNodeMsg crossMsg;
                crossMsg.set_target_user_id(uid);
                crossMsg.set_msg_type(chat::GROUP_CHAT_MSG);
                crossMsg.set_payload(reqPayload);
                string crossData = crossMsg.SerializeAsString();
                RedisMgr::instance()->publish("chat:cross_node", crossData);
            }
            else
            {
                // 全离线：存储离线消息到MySQL
                _offlineMsgModel->insert(uid, fromId, "group", content);
                offlineUserIds.push_back(uid);
            }
        }

        // 回执发送者
        chat::BaseMessage response;
        response.set_type(chat::GROUP_CHAT_MSG_ACK);
        chat::GroupChatAck ack;
        ack.set_code(0);
        ack.set_msg("Group message sent. Local online: " + to_string(localOnlineConns.size()) +
                    ", Cross-node: " + to_string(crossNodeUserIds.size() - offlineUserIds.size()) +
                    ", Offline stored: " + to_string(offlineUserIds.size()));
        response.set_payload(ack.SerializeAsString());
        string buf;
        response.SerializeToString(&buf);
        chatservice::sendFrame(conn, buf);
    });
}

// 注销：从_userConnMap移除，更新状态为offline
void chatservice::loginOut(const TcpConnectionPtr &conn, const string &js, Timestamp time)
{
    WT_LOG_INFO << "chatservice::loginOut() begin";
    chat::LoginRequest req;
    if (!req.ParseFromString(js))
    {
        WT_LOG_ERROR << "loginOut deserialization failed!";
        return;
    }
    int id = req.id();

    {
        lock_guard<mutex> lock(m_mutex);
        _userConnMap.erase(id);
    }

    User user;
    user.setId(id);
    user.setState("offline");
    _userModel->updateState(user);
    // 从Redis全局在线用户SET中移除
    RedisMgr::instance()->removeUserOnline(id);

    WT_LOG_INFO << "user " << id << " logged out";
}

// 初始化Redis：连接 + 启动SUBSCRIBE线程
// SUBSCRIBE线程收到跨节点消息后，查找target_user_id是否在本节点_userConnMap中，命中则转发
void chatservice::initRedis()
{
    if (!RedisMgr::instance()->connect("127.0.0.1", 6379))
    {
        WT_LOG_ERROR << "Redis connect failed, cross-node messaging disabled";
        return;
    }
    WT_LOG_INFO << "Redis connected, starting SUBSCRIBE thread for chat:cross_node";

    RedisMgr::instance()->startSubscribe("chat:cross_node", [this](const string &data) {
        // 此回调在Redis SUBSCRIBE线程中执行，非muduo IO线程
        // conn->send() 由muduo内部runInLoop保证线程安全，可直接调用
        chat::CrossNodeMsg crossMsg;
        if (!crossMsg.ParseFromString(data))
        {
            WT_LOG_ERROR << "CrossNodeMsg parse failed";
            return;
        }

        int targetUserId = crossMsg.target_user_id();
        TcpConnectionPtr toConn;
        {
            lock_guard<mutex> lock(m_mutex);
            auto it = _userConnMap.find(targetUserId);
            if (it != _userConnMap.end() && it->second->connected())
            {
                toConn = it->second;
            }
        }

        if (toConn)
        {
            // 目标用户在本节点在线：还原原始消息类型并转发
            chat::BaseMessage fwd;
            fwd.set_type(static_cast<chat::EnMsgType>(crossMsg.msg_type()));
            fwd.set_payload(crossMsg.payload());
            string fwdBuf;
            fwd.SerializeToString(&fwdBuf);
            chatservice::sendFrame(toConn, fwdBuf);
            WT_LOG_INFO << "Cross-node message forwarded to local user " << targetUserId;
        }
        // 未命中说明用户不在此节点，忽略（其他节点会处理）
    });
}
