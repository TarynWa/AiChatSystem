#ifndef OFFLINEMSGMODEL_H
#define OFFLINEMSGMODEL_H
#include "storage/mysql.hpp"
#include <vector>
using namespace std;

// OfflineMessage表的ORM类
// 新增字段：msg_id (幂等键，用于跨节点去重) / seq (顺序键，用于接收方重排)
class OfflineMsg
{
public:
    OfflineMsg(int id = 0, int userid = 0, int fromId = 0, string msgType = "", string content = "",
               int64_t msgId = 0, int seq = 0)
    {
        this->id = id;
        this->userid = userid;
        this->fromId = fromId;
        this->msgType = msgType;
        this->content = content;
        this->msgId = msgId;
        this->seq = seq;
    }

    void setId(int id) { this->id = id; }
    void setUserid(int userid) { this->userid = userid; }
    void setFromId(int fromId) { this->fromId = fromId; }
    void setMsgType(string msgType) { this->msgType = msgType; }
    void setContent(string content) { this->content = content; }
    void setMsgId(int64_t msgId) { this->msgId = msgId; }
    void setSeq(int seq) { this->seq = seq; }

    int getId() const { return this->id; }
    int getUserid() const { return this->userid; }
    int getFromId() const { return this->fromId; }
    string getMsgType() const { return this->msgType; }
    string getContent() const { return this->content; }
    int64_t getMsgId() const { return this->msgId; }
    int getSeq() const { return this->seq; }

private:
    int id;
    int userid;
    int fromId;
    string msgType;  // private / group
    string content;
    int64_t msgId;   // 幂等键：客户端生成的全局唯一ID
    int seq;          // 顺序键：每发送方单调递增
};

// OfflineMessage表的数据操作类
class OfflineMsgModel
{
public:
    // 存储离线消息（带 msg_id 与 seq）
    bool insert(int userid, int fromId, const string &msgType, const string &content,
                int64_t msgId = 0, int seq = 0);
    // 查询用户的离线消息（按 from_id, seq 升序返回，保证重排后顺序正确）
    vector<OfflineMsg> query(int userid);
    // 删除用户的全部离线消息（拉取后调用）
    bool remove(int userid);
};
#endif
