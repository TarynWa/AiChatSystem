#ifndef DB_H
#define DB_H

#include <mysql/mysql.h>
#include "AsyncLogging.hpp"
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>

using std::condition_variable;
using std::mutex;
using std::queue;
using std::shared_ptr;
using std::string;
using std::unique_lock;

class MysqlConnPool
{
public:
    static MysqlConnPool &instance()
    {
        static MysqlConnPool pool;
        return pool;
    }

    shared_ptr<MYSQL> getConnection()
    {
        unique_lock<mutex> lock(mutex_);
        while (connQueue_.empty())
        {
            cond_.wait(lock);
        }
        MYSQL *conn = connQueue_.front();
        connQueue_.pop();
        return shared_ptr<MYSQL>(conn, [this](MYSQL *ptr)
                                 { this->releaseConnection(ptr); });
    }

    void releaseConnection(MYSQL *conn)
    {
        if (!conn)
            return;
        unique_lock<mutex> lock(mutex_);
        connQueue_.push(conn);
        lock.unlock();
        cond_.notify_one();
    }

    ~MysqlConnPool();

private:
    MysqlConnPool(const string &host = "localhost",
                  const string &user = "root",
                  const string &passwd = "20050610",
                  const string &dbName = "chat",
                  unsigned int port = 3306,
                  size_t maxSize = 10);
    MYSQL *createConnection();

private:
    queue<MYSQL *> connQueue_;
    mutex mutex_;
    condition_variable cond_;
    string host_ = "localhost";
    string user_ = "root";
    string passwd_ = "20050610";
    string dbName_ = "chat";
    unsigned int port_{3306};
    size_t maxSize_{0};
};

class DB
{
public:
    DB()
    {
        WT_LOG_INFO << "DB constructor called";
        conn_ = nullptr;
    }
    explicit DB(shared_ptr<MYSQL> conn);
    ~DB() = default;

    bool connect();
    bool update(const string &sql);
    MYSQL_RES *query(const string &sql);
    bool startTransaction();
    bool commit();
    bool rollback();
    // 转义字符串中的 SQL 特殊字符（单引号、反斜杠等），防止注入与语法错误
    // 用于将用户内容安全拼接到 SQL 字符串字面量中
    string escape(const string &input);

private:
    shared_ptr<MYSQL> conn_;
};

#endif