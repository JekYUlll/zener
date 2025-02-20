#ifndef ZENER_DATABASE_H
#define ZENER_DATABASE_H

#include <mysql/mysql.h>

namespace zws {
namespace db {

class Database {
  public:
    virtual ~Database();
    virtual void Init() = 0;
    virtual void Close() = 0;
};

} // namespace db
} // namespace zws

#endif // !ZENER_DATABASE_H