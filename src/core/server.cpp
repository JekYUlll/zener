#include "core/server.h"
#include "log/logger.h"  // 或 "log/_logger.h"，取决于你想使用哪个日志实现

namespace zws {

    Server::Server(const int& port, const Database* db)
        : _port(port), _db(std::make_shared<Database>(db)) {
        LOG_I("SERVER START");
        
    }

    Server::~Server() {
        LOG_I("SERVER STOP");

    }

    void Server::Start() {
        Logger::Init();
        Logger::WriteToFile("logs/server.log");  // 建议使用固定的日志目录

        LOG_I(R"(
 _______ _ __   ___ _ __ 
|_  / _ \ '_ \ / _ \ '__|
 / /  __/ | | |  __/ |   
/___\___|_| |_|\___|_|   
)");
        
        LOG_I("Zener Web Server is starting...");
        LOG_I("Port: {}", _port);
        
    }

}