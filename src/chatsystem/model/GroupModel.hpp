#ifndef GROUPMODEL_H
#define GROUPMODEL_H
#include "Group.hpp"
#include "storage/mysql.hpp"
#include <vector>
using namespace std;

// GroupInfo + GroupMember表的数据操作类
class GroupModel
{
public:
    // 创建群组，成功后group.id被设置为服务端分配的群ID，并自动将创建者加入群成员（role=creator）
    bool createGroup(Group &group);
    // 加入群组
    bool joinGroup(int userid, int groupid, const string &role = "normal");
    // 退出群组
    bool quitGroup(int userid, int groupid);
    // 查询用户加入的所有群组
    vector<Group> queryGroups(int userid);
    // 查询群组成员列表
    vector<GroupUser> queryGroupMembers(int groupid);
};
#endif
