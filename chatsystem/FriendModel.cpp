#include "FriendModel.hpp"

// 双向插入好友关系，保证(userid,friendid)与(friendid,userid)同时存在
bool FriendModel::insert(int userid, int friendid)
{
    DB db;
    if (db.connect())
    {
        db.startTransaction();
        string sql1 = "INSERT INTO Friend(userid, friendid) VALUES(" + to_string(userid) + ", " + to_string(friendid) + ")";
        string sql2 = "INSERT INTO Friend(userid, friendid) VALUES(" + to_string(friendid) + ", " + to_string(userid) + ")";
        if (db.update(sql1) && db.update(sql2))
        {
            db.commit();
            return true;
        }
        else
        {
            db.rollback();
            return false;
        }
    }
    return false;
}

// 双向删除好友关系
bool FriendModel::remove(int userid, int friendid)
{
    DB db;
    if (db.connect())
    {
        db.startTransaction();
        string sql1 = "DELETE FROM Friend WHERE userid = " + to_string(userid) + " AND friendid = " + to_string(friendid);
        string sql2 = "DELETE FROM Friend WHERE userid = " + to_string(friendid) + " AND friendid = " + to_string(userid);
        if (db.update(sql1) && db.update(sql2))
        {
            db.commit();
            return true;
        }
        else
        {
            db.rollback();
            return false;
        }
    }
    return false;
}

// 查询用户的好友列表，关联User表获取好友基础信息
vector<User> FriendModel::queryFriends(int userid)
{
    WT_LOG_INFO << "FriendModel::queryFriends() called with userid: " << userid;
    vector<User> friends;
    DB db;
    if (db.connect())
    {
        string sql = "SELECT u.id, u.name, u.password, u.state, u.salt FROM User u "
                     "INNER JOIN Friend f ON u.id = f.friendid WHERE f.userid = " + to_string(userid);
        MYSQL_RES *res = db.query(sql);
        if (res)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)))
            {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setPwd(row[2]);
                user.setState(row[3]);
                user.setSalt(row[4] ? row[4] : "");
                friends.push_back(user);
            }
            mysql_free_result(res);
        }
    }
    return friends;
}
