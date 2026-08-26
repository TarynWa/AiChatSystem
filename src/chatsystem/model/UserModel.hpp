#ifndef USERMODEL_H
#define USERMODEL_H
#include"User.hpp"
#include "storage/mysql.hpp"
//User表的数据操作类
class UserModel
{
public:
    //添加用户（含密码哈希与盐值）
    bool insert(User &user);
    //根据用户ID查询用户
    User query(int id);
    //根据用户名查询用户（注册时用户名重复校验使用）
    User queryByName(const string &name);
    //更新用户状态
    void updateState(User &user);
};
#endif