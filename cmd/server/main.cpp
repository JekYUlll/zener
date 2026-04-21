#include "core/server.h"
#include "http/request.h"

int main() {
    zener::Logger::Init();

    const auto server = zener::NewServerFromConfig("config.toml");

    // Login: POST /login.html  { username, password }
    server->POST("/login.html", [](zener::http::Context& ctx) {
        const std::string user = ctx.GetPost("username");
        const std::string pwd  = ctx.GetPost("password");
        if (zener::http::Request::UserVerify(user, pwd, /*isLogin=*/true)) {
            ctx.Redirect("/welcome.html");
        } else {
            ctx.Redirect("/error.html");
        }
    });

    // Register: POST /register.html  { username, password }
    server->POST("/register.html", [](zener::http::Context& ctx) {
        const std::string user = ctx.GetPost("username");
        const std::string pwd  = ctx.GetPost("password");
        if (zener::http::Request::UserVerify(user, pwd, /*isLogin=*/false)) {
            ctx.Redirect("/login.html");
        } else {
            ctx.Redirect("/error.html");
        }
    });

    try {
        server->Run();
    } catch (const std::exception &) {
        //
    }

    return 0;
}
