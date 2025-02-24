#ifndef ZENER_SQL_CONNECT_POOL_H
#define ZENER_SQL_CONNECT_POOL_H

/// MYSQL 连接池
/// 11 中使用系统信号量，此处使用 C++ 标准库

/// 优化：
// 空闲连接池：std::deque<Connection*> 或 std::list<Connection*>
// 活跃连接集合：std::unordered_map<Connection*, std::chrono::time_point>
// 超时管理：优先队列（std::priority_queue）

// TODO 使用 boost 的无锁队列
// 可以使用 boost::lockfree::queue
// 使用基于数组的循环缓冲区（ring buffer）

// #include "database/db_mysql.h"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <mysql/mysql.h>
#include <queue>
// #include <semaphore.h>
// #include <boost/lockfree/queue.hpp>

namespace zws::db {

static constexpr int SQL_CONN_SIZE = 8;

class SqlConnector {
  public:
    static SqlConnector& GetInstance();

    SqlConnector(const SqlConnector& other) = delete;
    SqlConnector& operator=(const SqlConnector& rhs) = delete;
    ~SqlConnector() { Close(); }

    void Init(const char* host, unsigned int port, const char* user,
              const char* pwd, const char* dbName, int size = SQL_CONN_SIZE);

    void Close();

    MYSQL* GetConn();

    void FreeConn(MYSQL* sql); // 释放连接，归还池中

    size_t GetFreeConnCount() const;

    static int GetPoolSize() { return _maxConnSize; }

  private:
    SqlConnector() = default;


    static int _maxConnSize; // 连接队列最大大小
    std::queue<MYSQL*> _connQue;
    int _useCount{};

    // boost::lockfree::queue<MYSQL*> que;

    mutable std::mutex _mtx;
    std::condition_variable _condition;
};

} // namespace zws::db


#endif // !ZENER_SQL_CONNECT_POOL_H