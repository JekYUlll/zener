// #include "core/server.h"
#include "config/config.h"
#include "core/init.h"
#include "core/server.h"
#include "utils/log/logger.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sys/stat.h>

// [app]
// name = "zener"

// [log]
// level = "DEBUG"
// path = "logs"
// name = "logs/test.log"

// [mysql]
// host = "127.0.0.1"
// port = 3306
// user = "root"
// password = "root"
// database = "zener_test"

// [redis]
// host = "127.0.0.1"
// port = 6379
// password = "donotpanic"
// db = 0

int main(void) {

    if (0) {
        zws::Config::Init("config.toml");

        auto host = zws::GET_CONFIG("mysql.host");
        printf("%s\n", host.c_str());
    }

    if (1) {
        zws::Logger::Init();

        auto host = zws::GET_CONFIG("mysql.host");
        auto sqlPort = static_cast<unsigned int>(
            atoi(zws::GET_CONFIG("mysql.port").c_str()));
        auto sqlUser = zws::GET_CONFIG("mysql.user");
        auto sqlPassword = zws::GET_CONFIG("mysql.password");
        auto database = zws::GET_CONFIG("mysql.database");

        zws::v0::WebServer server(1316, 3, 60000, false, sqlPort,
                                  sqlUser.c_str(), sqlPassword.c_str(),
                                  database.c_str(), 12, 6, true, 1, 1024);

        server.Start();
    }

    // zws::v0::WebServer server(1316, 3, 60000, false, 3306, "root", "root",
    //                           "zener_test", 12, 6, true, 1, 1024);

    // auto server = new zws::Server(8080, nullptr);

    LOG_I("ZENER START");

    return EXIT_SUCCESS;
}
