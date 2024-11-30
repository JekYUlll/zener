#include "core/server.h"
#include "log/log.h"

namespace zws {

    Server::Server(const int& port, const Database* db)
        : _port(port), _db(std::make_shared<Database>(db)) {
        LOG_I("SERVER START");
        
    }

    Server::~Server() {
        LOG_I("SERVER STOP");

    }

}