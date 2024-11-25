#ifndef DATABASE_H
#define DATABASE_H

#include <string>

namespace zws {

    class Database
    {
    public:
        Database(std::string configPath);
        virtual ~Database();

    private:
        virtual void init() = 0;

    };

}

#endif // !DATABASE_H