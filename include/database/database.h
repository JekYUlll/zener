#ifndef DATABASE_H
#define DATABASE_H

#include <mysql/mysql.h>
#include <string>

namespace zws {

class Database {
  public:
    Database(std::string configPath);
    virtual ~Database();

  private:
    virtual void init() = 0;
    unsigned int _port;
};

} // namespace zws

#endif // !DATABASE_H