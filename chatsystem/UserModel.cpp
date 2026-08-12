#include "UserModel.hpp"

bool UserModel::insert(User &user)
{
    DB db;
    if(db.connect())
    {
        // Insert user into database
        db.startTransaction();
        string sql = "INSERT INTO User(name, password, state) VALUES('" + user.getName() + "', '" + user.getPwd() + "', '" + user.getState() + "')";
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
    return true;
}

User UserModel::query(int id)
{
    WT_LOG_INFO << "UserModel::query() called with id: " << id;
    DB db;
    User user;
    bool flag = db.connect();
    if(db.connect())
    {
        string sql = "SELECT * FROM User WHERE id = " + to_string(id);
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