#include "OfflineMsgModel.hpp"

// 存储离线消息
bool OfflineMsgModel::insert(int userid, int fromId, const string &msgType, const string &content)
{
    DB db;
    if (db.connect())
    {
        string sql = "INSERT INTO OfflineMessage(userid, from_id, msg_type, content) VALUES(" +
                     to_string(userid) + ", " + to_string(fromId) + ", '" + msgType + "', '" + content + "')";
        return db.update(sql);
    }
    return false;
}

// 查询用户的离线消息
vector<OfflineMsg> OfflineMsgModel::query(int userid)
{
    WT_LOG_INFO << "OfflineMsgModel::query() called with userid: " << userid;
    vector<OfflineMsg> msgs;
    DB db;
    if (db.connect())
    {
        string sql = "SELECT id, userid, from_id, msg_type, content FROM OfflineMessage WHERE userid = " + to_string(userid);
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
