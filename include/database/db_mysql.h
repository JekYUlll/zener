#ifndef DB_MYSQL_H
#define DB_MYSQL_H

#include <mysql/mysql.h>
#include <database/database.h>

namespace zws
{

    class MYSQL : public Database
    {

    public:

    private:
        std::string _userName;
        std::string _password;

    };
    
}

#endif // !DB_MYSQL_H