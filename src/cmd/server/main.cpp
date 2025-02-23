// #include "core/server.h"
#include "config/config.h"
#include "core/init.h"
#include "core/server.h"
#include "utils/log/logger.h"

#include <cstdlib>
#include <iostream>
#include <sys/stat.h>

static int LogToFile() {
    const char* log_path = "logs";
    if (mkdir(log_path, 0777) != 0 && errno != EEXIST) {
        std::cerr << "Failed to create log directory" << std::endl;
        return EXIT_FAILURE;
    }
    if (!zws::Logger::WriteToFile("logs/test.log")) {
        std::cerr << "Failed to create log file" << std::endl;
        return EXIT_FAILURE;
    }
    return 0;
}

int main(void) {

    if (0) {
        zws::Logger::Init();

        auto host = zws::GET_CONFIG("mysql.host");
        auto sqlPort =
            static_cast<unsigned int>(atoi(zws::GET_CONFIG("mysql.port")));
        auto sqlUser = zws::GET_CONFIG("mysql.user");
        auto sqlPassword = zws::GET_CONFIG("mysql.password");
        auto database = zws::GET_CONFIG("mysql.database");

        zws::v0::WebServer server(1316, 3, 60000, false, sqlPort, sqlUser,
                                  sqlPassword, database, 12, 6, true, 1, 1024);
    }

    zws::v0::WebServer server(1316, 3, 60000, false, 3306, "root", "root",
                              "zener_test", 12, 6, true, 1, 1024);

    server.Start();

    // auto server = new zws::Server(8080, nullptr);

    LOG_I("ZENER START");

    return EXIT_SUCCESS;
}
