#include "mysql.hpp"
#include <cstring>

MYSQL *MysqlConnPool::createConnection()
{
    WT_LOG_INFO << "Creating MySQL connection to " << host_ << ":" << port_ << " with user " << user_;
    MYSQL *conn = mysql_init(nullptr);
    if (!conn)
    {
        return nullptr;
    }
    if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(), passwd_.c_str(),
                            dbName_.c_str(), port_, nullptr, 0))
    {
        WT_LOG_ERROR << "MySQL connection failed: " << mysql_error(conn);
        mysql_close(conn);
        return nullptr;
    }
    WT_LOG_INFO << "MySQL connection established successfully.";
    return conn;
}

MysqlConnPool::~MysqlConnPool()
{
    unique_lock<mutex> lock(mutex_);
    while (!connQueue_.empty())
    {
        MYSQL *conn = connQueue_.front();
        connQueue_.pop();
        mysql_close(conn);
    }
}

MysqlConnPool::MysqlConnPool(const string &host,
                             const string &user,
                             const string &passwd,
                             const string &dbName,
                             unsigned int port,
                             size_t maxSize)
{
    WT_LOG_INFO << "Initializing MySQL connection pool with host: " << host << ", user: " << user << ", database: " << dbName << ", port: " << port << ", maxSize: " << maxSize;
    host_ = host;
    user_ = user;
    passwd_ = passwd;
    dbName_ = dbName;
    port_ = port;
    maxSize_ = maxSize;

    for (size_t i = 0; i < maxSize_; ++i)
    {
        MYSQL *conn = createConnection();
        if (!conn)
        {
            return;
        }
        connQueue_.push(conn);
    }
    WT_LOG_INFO << "MySQL connection pool initialized successfully with " << maxSize_ << " connections.";
    return;
}

DB::DB(shared_ptr<MYSQL> conn)
    : conn_(std::move(conn))
{
}

bool DB::connect()
{
    WT_LOG_INFO << "DB::connect() called";
    conn_ = MysqlConnPool::instance().getConnection();
    return conn_ != nullptr;
    WT_LOG_INFO << "DB::connect() finished";
}

bool DB::update(const string &sql)
{
    if (!conn_)
        return false;
    return mysql_query(conn_.get(), sql.c_str()) == 0;
}

MYSQL_RES *DB::query(const string &sql)
{
    if (!conn_)
        return nullptr;
    if (mysql_query(conn_.get(), sql.c_str()) != 0)
    {
        return nullptr;
    }
    return mysql_store_result(conn_.get());
}

bool DB::startTransaction()
{
    if (!conn_)
        return false;
    return mysql_query(conn_.get(), "START TRANSACTION") == 0;
}

bool DB::commit()
{
    if (!conn_)
        return false;
    return mysql_query(conn_.get(), "COMMIT") == 0;
}

bool DB::rollback()
{
    if (!conn_)
        return false;
    return mysql_query(conn_.get(), "ROLLBACK") == 0;
}