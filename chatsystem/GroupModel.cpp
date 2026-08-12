#include "GroupModel.hpp"

// 创建群组：插入GroupInfo后获取自增ID，再将创建者加入GroupMember
bool GroupModel::createGroup(Group &group)
{
    DB db;
    if (db.connect())
    {
        db.startTransaction();
        string sql = "INSERT INTO GroupInfo(group_name, group_desc, creator_id) VALUES('" +
                     group.getName() + "', '" + group.getDesc() + "', " + to_string(group.getCreatorId()) + ")";
        if (db.update(sql))
        {
            // 获取自增群ID
            MYSQL_RES *res = db.query("SELECT LAST_INSERT_ID()");
            if (res)
            {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0])
                {
                    group.setId(atoi(row[0]));
                }
                mysql_free_result(res);
            }
            // 将创建者加入群成员，role=creator
            string memberSql = "INSERT INTO GroupMember(groupid, userid, role) VALUES(" +
                                to_string(group.getId()) + ", " + to_string(group.getCreatorId()) + ", 'creator')";
            if (db.update(memberSql))
            {
                db.commit();
                return true;
            }
        }
        db.rollback();
        return false;
    }
    return false;
}

// 加入群组
bool GroupModel::joinGroup(int userid, int groupid, const string &role)
{
    DB db;
    if (db.connect())
    {
        string sql = "INSERT INTO GroupMember(groupid, userid, role) VALUES(" +
                     to_string(groupid) + ", " + to_string(userid) + ", '" + role + "')";
        return db.update(sql);
    }
    return false;
}

// 退出群组
bool GroupModel::quitGroup(int userid, int groupid)
{
    DB db;
    if (db.connect())
    {
        string sql = "DELETE FROM GroupMember WHERE groupid = " + to_string(groupid) +
                     " AND userid = " + to_string(userid);
        return db.update(sql);
    }
    return false;
}

// 查询用户加入的所有群组
vector<Group> GroupModel::queryGroups(int userid)
{
    WT_LOG_INFO << "GroupModel::queryGroups() called with userid: " << userid;
    vector<Group> groups;
    DB db;
    if (db.connect())
    {
        string sql = "SELECT g.id, g.group_name, g.group_desc, g.creator_id FROM GroupInfo g "
                     "INNER JOIN GroupMember m ON g.id = m.groupid WHERE m.userid = " + to_string(userid);
        MYSQL_RES *res = db.query(sql);
        if (res)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)))
            {
                Group group;
                group.setId(atoi(row[0]));
                group.setName(row[1]);
                group.setDesc(row[2]);
                group.setCreatorId(atoi(row[3]));
                groups.push_back(group);
            }
            mysql_free_result(res);
        }
    }
    return groups;
}

// 查询群组成员列表，关联User表获取成员基础信息
vector<GroupUser> GroupModel::queryGroupMembers(int groupid)
{
    WT_LOG_INFO << "GroupModel::queryGroupMembers() called with groupid: " << groupid;
    vector<GroupUser> members;
    DB db;
    if (db.connect())
    {
        string sql = "SELECT u.id, u.name, u.password, u.state, u.salt, m.role FROM User u "
                     "INNER JOIN GroupMember m ON u.id = m.userid WHERE m.groupid = " + to_string(groupid);
        MYSQL_RES *res = db.query(sql);
        if (res)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)))
            {
                GroupUser user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setPwd(row[2]);
                user.setState(row[3]);
                user.setSalt(row[4] ? row[4] : "");
                user.setRole(row[5] ? row[5] : "normal");
                members.push_back(user);
            }
            mysql_free_result(res);
        }
    }
    return members;
}
