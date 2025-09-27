#include "http/request.h"
#include "http/response.h"

#include <functional>
#include <string>
#include <unordered_map>

// TODO:
// 实现路由组、路由树

namespace zener {
namespace fw {

typedef std::function<bool(http::Request, http::Response)> HandlerFunc;

class RestFull {
  public:
    RestFull() {}
    ~RestFull() {}

    virtual void GET(const std::string& path, HandlerFunc handler) = 0;
    virtual void POST(const std::string& path, HandlerFunc handler) = 0;

    void handleRequest() {
        // Handle RESTful request
    }
};

class Router : public RestFull {
  public:
    Router() {}
    ~Router() {}

  private:
    void addRoute(const std::string& path, HandlerFunc handler) {
        // Add route to the router
    }
    void routeRequest(const std::string& path) {
        // Route the request to the appropriate handler
    }

    std::unordered_map<std::string, HandlerFunc> _handlers;
};

} // namespace fw

} // namespace zener
