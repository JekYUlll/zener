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
        void Default();
        void Stop();

    private:

        int _port;
        std::shared_ptr<Database> _db;
        // std::unique_ptr<ThreadPool> m_threadPool;
        // std::unique_ptr<EventLoop> m_loop;

    };

}

#endif // !SERVER_H
