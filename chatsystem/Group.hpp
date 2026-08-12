#ifndef GROUP_H
#define GROUP_H

#include <string>
#include <vector>
#include "User.hpp"
using namespace std;

// GroupInfo表的ORM类
class Group
{
public:
    Group(int id = -1, string name = "", string desc = "", int creatorId = -1)
    {
        this->id = id;
        this->name = name;
        this->desc = desc;
        this->creatorId = creatorId;
    }

    void setId(int id) { this->id = id; }
    void setName(string name) { this->name = name; }
    void setDesc(string desc) { this->desc = desc; }
    void setCreatorId(int creatorId) { this->creatorId = creatorId; }

    int getId() const { return this->id; }
    string getName() const { return this->name; }
    string getDesc() const { return this->desc; }
    int getCreatorId() const { return this->creatorId; }

private:
    int id;
    string name;
    string desc;
    int creatorId;
};

// GroupMember表的ORM类，继承User以复用用户基础信息
class GroupUser : public User
{
public:
    GroupUser() : User(), role("normal") {}
    void setRole(string role) { this->role = role; }
    string getRole() const { return this->role; }

private:
    string role; // 群内角色：creator/normal
};

#endif
