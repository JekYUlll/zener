#ifndef SERVER_H
#define SERVER_H

#include <memory>
#include "database/database.h"

namespace zws {

    class Server
    {
    public:
        Server(const int& port, const Database* db);
        ~Server();

        void Start();
        void Stop();

    private:

        std::shared_ptr<Database> m_Db;

    };

}




#endif // !SERVER_H
