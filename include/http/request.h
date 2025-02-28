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

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace zener::http {

class Request {
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

    // 初始化无所谓，在Init中
    Request() : _state(REQUEST_LINE) { Init(); }
    ~Request() = default;
    // TODO 本实现在 Coon 移动的时候也进行了移动
    // 是否允许？是否安全？
    Request(const Request&) = default;
    Request(Request&&) = default;
    Request& operator=(Request&&) = default;

    void Init();
    [[nodiscard]] bool parse(Buffer& buff);

    _ZENER_SHORT_FUNC std::string Path() const { return _path; }
    _ZENER_SHORT_FUNC std::string& Path() { return _path; }

    std::string Method() const;
    std::string Version() const;
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    [[nodiscard]] bool IsKeepAlive() const;

    /*
    TODO
    void HttpConn::ParseFormData() {}
    void HttpConn::ParseJson() {}
    */

  private:
    bool parseRequestLine(const std::string& line);
    void parseHeader(const std::string& line);
    void parseBody(const std::string& line);

    void parsePath();
    void parsePost();
    void parseFromUrlencoded();

    static bool userVerify(const std::string& name, const std::string& pwd,
                           bool isLogin);

    PARSE_STATE _state;
    std::string _method, _path, _version, _body;
    std::unordered_map<std::string, std::string> _header;
    std::unordered_map<std::string, std::string> _post;

    static const std::unordered_set<std::string> DEFAULT_HTML;
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;
    static int convertHex(char ch);
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

} // namespace zener::http

#endif // ZENER_HTTP_REQUEST_H