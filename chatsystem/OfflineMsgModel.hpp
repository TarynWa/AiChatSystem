#ifndef OFFLINEMSGMODEL_H
#define OFFLINEMSGMODEL_H
#include "mysql.hpp"
#include <vector>
using namespace std;

// OfflineMessage表的ORM类
class OfflineMsg
{
public:
    OfflineMsg(int id = 0, int userid = 0, int fromId = 0, string msgType = "", string content = "")
    {
        this->id = id;
        this->userid = userid;
        this->fromId = fromId;
        this->msgType = msgType;
        this->content = content;
    }

    void setId(int id) { this->id = id; }
    void setUserid(int userid) { this->userid = userid; }
    void setFromId(int fromId) { this->fromId = fromId; }
    void setMsgType(string msgType) { this->msgType = msgType; }
    void setContent(string content) { this->content = content; }

    int getId() const { return this->id; }
    int getUserid() const { return this->userid; }
    int getFromId() const { return this->fromId; }
    string getMsgType() const { return this->msgType; }
    string getContent() const { return this->content; }

private:
    int id;
    int userid;
    int fromId;
    string msgType;  // private / group
    string content;
};

// OfflineMessage表的数据操作类
class OfflineMsgModel
{
public:
    // 存储离线消息
    bool insert(int userid, int fromId, const string &msgType, const string &content);
    // 查询用户的离线消息
    vector<OfflineMsg> query(int userid);
    // 删除用户的全部离线消息（拉取后调用）
    bool remove(int userid);
};
#endif
