#ifndef ZENER_RESTFUL_H
#define ZENER_RESTFUL_H

#include "http/request.h"
#include "http/response.h"

#include <functional>
#include <string>

class IRestful {
  protected:
    using Handler = typename std::function<void(
        const zener::http::HttpRequest& req, zener::http::HttpResponse& res)>;

  public:
    virtual ~IRestful() {}

    virtual void GET(const std::string& router, Handler h) = 0;
    virtual void POST(const std::string& router, Handler h) = 0;
    virtual void PUT(const std::string& router, Handler h) = 0;
    virtual void HEAD(const std::string& router, Handler h) = 0;
    // virtual void DELETE(const std::string& router, Handler h) = 0;
    // virtual void OPTIONS(const std::string& router, Handler h) = 0;
    // virtual void TRACE(const std::string& router, Handler h) = 0;
    // virtual void CONNECT(const std::string& router, Handler h) = 0;
    // virtual void PATCH(const std::string& router, Handler h) = 0;
};

#endif // !ZENER_RESTFUL_H