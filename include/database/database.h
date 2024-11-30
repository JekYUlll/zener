#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <mysql/mysql.h>

namespace zws 
{

    class Database
    {
    public:
        Database(std::string configPath);
        virtual ~Database();

    private:
        virtual void init() = 0;
        unsigned int _port;

    };

}

#endif // !DATABASE_H