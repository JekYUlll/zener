#ifndef ZENER_CONN_RAII_HPP
#define ZENER_CONN_RAII_HPP

#include "database/sql_connector.h"

#include <cassert>
#include <mysql/mysql.h>

namespace zws {
namespace db {

class SqlConnRAII {

  public:
    SqlConnRAII(MYSQL** psql, SqlConnector* connPool) {
        assert(connPool);
        *psql = connPool->GetConn();
        _sql = *psql;
        _connPool = connPool;
    }

    ~SqlConnRAII() {
        if (_sql) {
            _connPool->FreeConn(_sql);
        }
    }

  private:
    MYSQL* _sql;
    SqlConnector* _connPool;
};

} // namespace db
} // namespace zws

#endif // !ZENER_CONN_RAII_HPP