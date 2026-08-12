#ifndef USERMODEL_H
#define USERMODEL_H
#include"User.hpp"
#include "mysql.hpp"
//User表的数据操作类
class UserModel
{
public:
    //添加用户
    bool insert(User &user);
    //查询用户
    User query(int id);
    //更新用户状态
    void updateState(User &user);
};
#endif