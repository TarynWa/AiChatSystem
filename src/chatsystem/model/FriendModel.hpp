#ifndef FRIENDMODEL_H
#define FRIENDMODEL_H
#include "User.hpp"
#include "storage/mysql.hpp"
#include <vector>
using namespace std;

// Friend表的数据操作类
class FriendModel
{
public:
    // 添加好友关系（双向插入，保证双向一致）
    bool insert(int userid, int friendid);
    // 删除好友关系（双向删除）
    bool remove(int userid, int friendid);
    // 查询用户的好友列表
    vector<User> queryFriends(int userid);
};
#endif
