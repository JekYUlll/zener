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

// 整个项目最麻烦的地方：字符串解析。直接抄了。

#include "buffer/buffer.h"
// #include "common.h"
// #include "file/file.h"
// #include "http/entity.h"
// #include "http/header.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace zener {
namespace http {

// enum class HttpMethod {
//     GET,
//     PUT,
//     POST,
//     PATCH,
// };

class Request : std::enable_shared_from_this<Request> {
  public:
    enum PARSE_STATE {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,
    };

    enum HTTP_CODE {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURSE,
        FORBIDDENT_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
    };

    Request() { Init(); }
    ~Request() = default;

    void Init();
    bool parse(Buffer& buff);

    _ZENER_SHORT_FUNC const std::string path() const { return path_; }
    _ZENER_SHORT_FUNC std::string& path() { return path_; }

    std::string method() const;
    std::string version() const;
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    bool IsKeepAlive() const;

    /*
    todo
    void HttpConn::ParseFormData() {}
    void HttpConn::ParseJson() {}
    */

  private:
    bool ParseRequestLine_(const std::string& line);
    void ParseHeader_(const std::string& line);
    void ParseBody_(const std::string& line);

    void ParsePath_();
    void ParsePost_();
    void ParseFromUrlencoded_();

    static bool UserVerify(const std::string& name, const std::string& pwd,
                           bool isLogin);

    PARSE_STATE state_;
    std::string method_, path_, version_, body_;
    std::unordered_map<std::string, std::string> header_;
    std::unordered_map<std::string, std::string> post_;

    static const std::unordered_set<std::string> DEFAULT_HTML;
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;
    static int ConverHex(char ch);
};

// class Request {
//   public:
//     Request();
//     ~Request();

//     std::string Method() const;
//     std::string Path() const;
//     std::unordered_map<std::string, std::string> Query() const;
//     std::unordered_map<std::string, std::string> Headers() const;
//     std::string Body() const;

//     std::vector<File*> Files() const;

//   private:
//     Entity _entity;
//     std::unique_ptr<Header> _pHeader;
// };

} // namespace http
} // namespace zener

#endif // ZENER_HTTP_REQUEST_H