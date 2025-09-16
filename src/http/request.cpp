#include "http/request.h"
#include "database/sql_connector.h"
#include "database/sqlconnRAII.hpp"
#include "utils/log/logger.h"

#include <mysql/mysql.h>
// TODO: 替换为boost::regex
#include <boost/regex.hpp>
// #include <regex>

namespace zener::http {

const std::unordered_set<std::string> Request::DEFAULT_HTML{
    "/index", "/register", "/login", "/welcome", "/video", "/picture",
};

const std::unordered_map<std::string, int> Request::DEFAULT_HTML_TAG{
    {"/register.html", 0},
    {"/login.html", 1},
};

void Request::Init() {
    _method = _path = _version = _body = "";
    _state = REQUEST_LINE;
    _header.clear();
    _post.clear();
}

bool Request::IsKeepAlive() const {
    if (_header.count("Connection") == 1) {
        return _header.find("Connection")->second == "keep-alive" &&
               _version == "1.1";
    }
    return false;
}

bool Request::parse(Buffer &buff) {
    constexpr char CRLF[] = "\r\n";
    if (buff.ReadableBytes() <= 0) {
        return false;
    }
    while (buff.ReadableBytes() && _state != FINISH) {
        const char *lineEnd =
            std::search(buff.Peek(), buff.BeginWrite(), CRLF, CRLF + 2);
        const char *peek = buff.Peek();
        std::string line(peek, lineEnd);
        switch (_state) {
        case REQUEST_LINE:
            if (!parseRequestLine(line)) {
                return false;
            }
            parsePath();
            break;
        case HEADERS:
            parseHeader(line);
            if (buff.ReadableBytes() <= 2) {
                _state = FINISH;
            }
            break;
        case BODY:
            parseBody(line);
            break;
        default:
            break;
        }
        if (lineEnd == buff.BeginWrite()) {
            break;
        }
        buff.RetrieveUntil(lineEnd + 2);
    }
    LOG_D("{}, {}, {}", _method, _path, _version);
    return true;
}

void Request::parsePath() {
    if (_path == "/") {
        _path = "/index.html";
    } else {
        for (auto &item : DEFAULT_HTML) {
            if (item == _path) {
                _path += ".html";
                break;
            }
        }
    }
}

bool Request::parseRequestLine(const std::string &line) {
    // 正则
    const boost::regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    if (boost::smatch subMatch; boost::regex_match(line, subMatch, patten)) {
        _method = subMatch[1];
        _path = subMatch[2];
        _version = subMatch[3];
        _state = HEADERS;
        return true;
    }
    LOG_W("RequestLine Error! line: {}", line);
    return false;
}

void Request::parseHeader(const std::string &line) {
    const boost::regex patten("^([^:]*): ?(.*)$");
    if (boost::smatch subMatch; boost::regex_match(line, subMatch, patten)) {
        _header[subMatch[1]] = subMatch[2];
    } else {
        _state = BODY;
    }
}

void Request::parseBody(const std::string &line) {
    _body = line;
    parsePost();
    _state = FINISH;
    LOG_D("Body:{}, len:{}", line, line.length());
}

int Request::convertHex(const char ch) {
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    return ch;
}

void Request::parsePost() {
    if (_method == "POST" &&
        _header["Content-Type"] == "application/x-www-form-urlencoded") {
        parseFromUrlencoded();
        if (DEFAULT_HTML_TAG.count(_path)) {
            int tag = DEFAULT_HTML_TAG.find(_path)->second;
            LOG_D("Tag:{}", tag);
            if (tag == 0 || tag == 1) {
                if (const bool isLogin = (tag == 1);
                    userVerify(_post["username"], _post["password"], isLogin)) {
                    _path = "/welcome.html";
                } else {
                    _path = "/error.html";
                }
            }
        }
    }
}

void Request::parseFromUrlencoded() {
    if (_body.empty()) {
        return;
    }
    std::string key, value;
    int num = 0;
    const size_t n = _body.size();
    int i = 0, j = 0;
    for (; i < n; i++) {
        const char ch = _body[i];
        switch (ch) {
        case '=':
            key = _body.substr(j, i - j);
            j = i + 1;
            break;
        case '+':
            _body[i] = ' ';
            break;
        case '%':
            num = convertHex(_body[i + 1]) * 16 + convertHex(_body[i + 2]);
            _body[i + 2] = num % 10 + '0';
            _body[i + 1] = num / 10 + '0';
            i += 2;
            break;
        case '&':
            value = _body.substr(j, i - j);
            j = i + 1;
            _post[key] = value;
            LOG_D("{} = {}", key, value);
            break;
        default:
            break;
        }
    }
    assert(j <= i);
    if (_post.count(key) == 0 && j < i) {
        value = _body.substr(j, i - j);
        _post[key] = value;
    }
}

bool Request::userVerify(const std::string &name, const std::string &pwd,
                         const bool isLogin) {
    if (name.empty() || pwd.empty()) {
        return false;
    }
    LOG_I("Verify name:{0} pwd:{1}", name.c_str(), pwd.c_str());
    MYSQL *sql;
    db::SqlConnRAII raii(&sql, &db::SqlConnector::GetInstance());
    assert(sql);

    bool flag = false;
    unsigned int j = 0;
    char order[256] = {0};
    MYSQL_FIELD *fields = nullptr;
    MYSQL_RES *res = nullptr;

    if (!isLogin) {
        flag = true;
    }
    /* 查询用户及密码 */
    snprintf(order, 256,
             "SELECT username, password FROM user WHERE username='%s' LIMIT 1",
             name.c_str());
    LOG_D("{}", order);

    if (mysql_query(sql, order)) {
        /*
            逻辑疑似错误
        */
        mysql_free_result(res);
        return false;
    }
    res = mysql_store_result(sql);
    if (!res) {
        LOG_E("MYSQL store result failed: {}", mysql_error(sql));
        return false;
    }
    /*
        mysql_num_fields主要用于 ​获取
        MySQL查询结果集的字段数量（列数）
    */
    j = mysql_num_fields(res);
    fields = mysql_fetch_fields(res);
    if (fields) {
        for (unsigned int i = 0; i < j; i++) {
            LOG_I("Field {}: name={}, type={}", i, fields[i].name,
                  fields[i].type);
        }
    }

    while (MYSQL_ROW row = mysql_fetch_row(res)) {
        LOG_D("MYSQL ROW: {0} {1}", row[0], row[1]);
        std::string password(row[1]);
        /* 注册行为 且 用户名未被使用*/
        if (isLogin) {
            if (pwd == password) {
                flag = true;
            } else {
                flag = false;
                LOG_D("Pwd error!");
            }
        } else {
            flag = false;
            LOG_D("User used!");
        }
    }
    mysql_free_result(res);
    /* 注册行为 且 用户名未被使用*/
    if (!isLogin && flag == true) {
        LOG_D("Regirster.");
        bzero(order, 256);
        snprintf(order, 256,
                 "INSERT INTO user(username, password) VALUES('%s','%s')",
                 name.c_str(), pwd.c_str());
        LOG_D("%s", order);
        if (mysql_query(sql, order)) {
            LOG_D("Insert error!");
            flag = false;
        }
        flag = true;
    }
    // db::SqlConnector::GetInstance().FreeConn(sql); // RAII 处理
    LOG_D("User {} verify success!", name);
    return flag;
}

} // namespace zener::http
