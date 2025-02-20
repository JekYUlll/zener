#ifndef ZENER_HTTP_RESPONSE_H
#define ZENER_HTTP_RESPONSE_H

#include <string>

namespace zws {
namespace http {

enum class StatusCode {
    HTTP_OK = 200,
    HTTP_CREATED = 201,
    HTTP_BAD_REQUEST = 400,
    HTTP_NOT_FOUND = 404,
    HTTP_INTERNAL_SERVER_ERROR = 500,
};

const char* StatusCodeToString(StatusCode code);

class Response {
  public:
    Response() {}
    ~Response() {}

    Response& Status(int code);
    Response& SetHeader(const std::string& key, const std::string& vaule);
    void Send(const std::string& content);
    // void Json(const Json& data);
    void SendFile(const std::string& path);

  private:
    StatusCode code;
};

} // namespace http
} // namespace zws

#endif // !ZENER_HTTP_RESPONSE_H