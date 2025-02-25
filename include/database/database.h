#ifndef ZENER_DATABASE_H
#define ZENER_DATABASE_H

#include <mysql/mysql.h>

namespace zener {
namespace db {

class Database {
  public:
    virtual ~Database();
    virtual void Init() = 0;
    virtual void Close() = 0;
};

} // namespace db
} // namespace zener

#endif // !ZENER_DATABASE_H