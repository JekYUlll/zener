#include "database/sql_connector.h"
#include "utils/log/logger.h"

#include <cassert>

namespace zener::db {

int SqlConnector::_maxConnSize;

SqlConnector& SqlConnector::GetInstance() {
    static SqlConnector instance;
    return instance;
}

void SqlConnector::Init(const char* host, const unsigned int port,
                        const char* user, const char* pwd, const char* dbName,
                        const int size) {
    assert(size > 0);
    for (int i = 0; i < size; i++) {
        MYSQL* sql = nullptr;
        sql = mysql_init(sql);
        if (!sql) {
            LOG_E("MYSQL[{}] init error!", i);
            assert(sql);
            continue;
        }
        sql =
            mysql_real_connect(sql, host, user, pwd, dbName, port, nullptr, 0);
        if (!sql) {
            LOG_E("MYSQL[{}] connect error!", i);  // TODO 重连
            continue;
        }
        LOG_I("Connected to MYSQL[{}], database: {}.", i, dbName);
        _connQue.push(sql);
    }
    _maxConnSize = size;
}

MYSQL* SqlConnector::GetConn() {
    MYSQL* sql = nullptr;
    {
        std::unique_lock locker(_mtx);
        // 如果没连接 MYSQL，会触发 busy ，信号退出：
        // Process finished with exit code 139 (interrupted by signal 11:SIGSEGV)
        if (_connQue.empty()) {
            LOG_W("SQL connect pool busy!");
        } else {
            _condition.wait(locker,
                            [this]() { return !this->_connQue.empty(); });
        }
        sql = _connQue.front();
        _connQue.pop();
    }
    return sql;
}

void SqlConnector::FreeConn(MYSQL* sql) {
    assert(sql);
    {
        std::lock_guard<std::mutex> locker(_mtx);
        _connQue.push(sql); // 放回连接池
    }
}

void SqlConnector::Close() {
    {
        std::lock_guard locker(_mtx);
        while (!_connQue.empty()) {
            const auto sql = _connQue.front();
            _connQue.pop();
            mysql_close(sql);
        }
        mysql_library_end();
    }
}

size_t SqlConnector::GetFreeConnCount() const {
    std::lock_guard locker(_mtx);
    return _connQue.size();
}

} // namespace zener::db
