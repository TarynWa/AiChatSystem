#include "UserModel.hpp"

bool UserModel::insert(User &user)
{
    DB db;
    if(db.connect())
    {
        // Insert user into database (password字段存储加盐哈希值，禁止明文)
        db.startTransaction();
        string sql = "INSERT INTO User(name, password, state, salt) VALUES('" + user.getName() + "', '" + user.getPwd() + "', '" + user.getState() + "', '" + user.getSalt() + "')";
        if(db.update(sql))
        {
            // Get the inserted user's ID
            MYSQL_RES *res = db.query("SELECT LAST_INSERT_ID()");
            if(res)
            {
                MYSQL_ROW row = mysql_fetch_row(res);
                if(row && row[0])
                {
                    user.setId(atoi(row[0]));
                }
                mysql_free_result(res);
            }
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

// 根据用户名查询用户，用于注册时用户名重复校验
// 与query(id)的区别：显式列出列名以读取salt字段，避免SELECT *受列顺序影响
User UserModel::queryByName(const string &name)
{
    WT_LOG_INFO << "UserModel::queryByName() called with name: " << name;
    DB db;
    User user;
    if(db.connect())
    {
        string sql = "SELECT id, name, password, state, salt FROM User WHERE name = '" + name + "'";
        MYSQL_RES *res = db.query(sql);
        if(res)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if(row)
            {
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setPwd(row[2]);
                user.setState(row[3]);
                user.setSalt(row[4] ? row[4] : "");
            }
            mysql_free_result(res);
        }
    }
    return user;
}

User UserModel::query(int id)
{
    WT_LOG_INFO << "UserModel::query() called with id: " << id;
    DB db;
    User user;
    if(db.connect())
    {
        // 显式列出列名，避免 SELECT * 受建表列顺序影响，并读取salt用于密码校验
        string sql = "SELECT id, name, password, state, salt FROM User WHERE id = " + to_string(id);
        MYSQL_RES *res = db.query(sql);
        if(res)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if(row)
            {
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setPwd(row[2]);
                user.setState(row[3]);
                user.setSalt(row[4] ? row[4] : "");
            }
            mysql_free_result(res);
        }
    }
    return user;
}

void UserModel::updateState(User &user)
{
    DB db;
    if(db.connect())
    {
       db.startTransaction();
        string sql = "UPDATE User SET state = '" + user.getState() + "' WHERE id = " + to_string(user.getId());
        if(db.update(sql))
        {
            db.commit();
        }
        else
        {
            db.rollback();
        }
    }
}