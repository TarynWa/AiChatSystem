#ifndef USER_H
#define USER_H

#include <string>
using namespace std;
#include"AsyncLogging.hpp"
// User表的ORM类
class User
{
public:
    User(int id = -1, string name = "", string pwd = "", string state = "offline")
    {
        WT_LOG_INFO << "User constructor called with id: " << id << ", name: " << name << ", password: " << pwd << ", state: " << state;
        this->id = id;
        this->name = name;
        this->password = pwd;
        this->state = state;
        this->salt = "";
    }

    void setId(int id) { this->id = id; }
    void setName(string name) { this->name = name; }
    void setPwd(string pwd) { this->password = pwd; }
    void setState(string state) { this->state = state; }
    // 设置密码盐值（注册时随机生成）
    void setSalt(string salt) { this->salt = salt; }

    int getId() const { return this->id; }
    string getName() const { return this->name; }
    string getPwd() const { return this->password; }
    string getState() const { return this->state; }
    // 获取密码盐值（登录校验时使用）
    string getSalt() const { return this->salt; }

protected:
    int id;
    string name;
    string password; // 存储加盐SHA256哈希值，禁止明文
    string state;
    string salt;     // 密码盐值，配合password字段完成密码校验
};

#endif