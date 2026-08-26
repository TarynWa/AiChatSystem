#include "OfflineMsgModel.hpp"

// 存储离线消息（带 msg_id 与 seq，支持跨节点去重与接收方重排）
bool OfflineMsgModel::insert(int userid, int fromId, const string &msgType, const string &content,
                             int64_t msgId, int seq)
{
    DB db;
    if (db.connect())
    {
        // INSERT 时带上 msg_id 和 seq，便于查询时按 (from_id, seq) 排序
        string sql = "INSERT INTO OfflineMessage(userid, from_id, msg_type, content, msg_id, seq) VALUES(" +
                     to_string(userid) + ", " + to_string(fromId) + ", '" + msgType + "', '" +
                     db.escape(content) + "', " + to_string(msgId) + ", " + to_string(seq) + ")";
        return db.update(sql);
    }
    return false;
}

// 查询用户的离线消息（按 from_id, seq 升序返回，保证重排后顺序正确）
vector<OfflineMsg> OfflineMsgModel::query(int userid)
{
    WT_LOG_INFO << "OfflineMsgModel::query() called with userid: " << userid;
    vector<OfflineMsg> msgs;
    DB db;
    if (db.connect())
    {
        // ORDER BY from_id, seq：同一发送方的消息按 seq 升序，便于接收方直接顺序投递
        string sql = "SELECT id, userid, from_id, msg_type, content, msg_id, seq FROM OfflineMessage "
                     "WHERE userid = " + to_string(userid) + " ORDER BY from_id ASC, seq ASC";
        MYSQL_RES *res = db.query(sql);
        if (res)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)))
            {
                OfflineMsg msg;
                msg.setId(atoi(row[0]));
                msg.setUserid(atoi(row[1]));
                msg.setFromId(atoi(row[2]));
                msg.setMsgType(row[3] ? row[3] : "");
                msg.setContent(row[4] ? row[4] : "");
                msg.setMsgId(row[5] ? atoll(row[5]) : 0);
                msg.setSeq(row[6] ? atoi(row[6]) : 0);
                msgs.push_back(msg);
            }
            mysql_free_result(res);
        }
    }
    return msgs;
}

// 删除用户的全部离线消息
bool OfflineMsgModel::remove(int userid)
{
    DB db;
    if (db.connect())
    {
        string sql = "DELETE FROM OfflineMessage WHERE userid = " + to_string(userid);
        return db.update(sql);
    }
    return false;
}
