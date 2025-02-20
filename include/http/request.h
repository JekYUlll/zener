#ifndef ZENER_HTTP_REQUEST_H
#define ZENER_HTTP_REQUEST_H

/*
POST http://www.baidu.com HTTP/1.1                         \r\n       (请求行)
Host: api.efxnow.com                                       \r\n (一条请求头)
Content-Type: application/x-www-form-urlencoded            \r\n (一条请求头)
Content-Length: length                                     \r\n (一条请求头)
                                                           \r\n       (空行)
UserID=string&PWD=string&OrderConfirmation=string                     (请求体)
*/

#include "common.h"
#include "file/file.h"
#include "http/entity.h"
#include "http/header.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace zws {
namespace http {

// HTTP + 加密 + 认证 + 完整性保护 = HTTPS

enum class HttpMethod {
    GET,
    PUT,
    POST,
    PATCH,
};

class Request {
  public:
    Request();
    ~Request();

    std::string Method() const;
    std::string Path() const;
    std::unordered_map<std::string, std::string> Query() const;
    std::unordered_map<std::string, std::string> Headers() const;
    std::string Body() const;

    std::vector<File*> Files() const;

  private:
    Entity _entity;
    std::unique_ptr<Header> _pHeader;
};

} // namespace http
} // namespace zws

#endif // ZENER_HTTP_REQUEST_H