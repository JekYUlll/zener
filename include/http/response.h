#ifndef ZENER_HTTP_RESPONSE_H
#define ZENER_HTTP_RESPONSE_H

#include "buffer/buffer.h"

#include <string>
#include <sys/stat.h>
#include <unordered_map>

namespace zener::http {

class Response {
  public:
    Response();
    ~Response();
    Response(Response&&) = default;
    Response& operator=(Response&&) = default;

    void Init(const std::string& staticDir, const std::string& path,
              bool isKeepAlive, int code);
    void MakeResponse(Buffer& buff);
    void UnmapFile();
    void ErrorContent(Buffer& buff, const std::string& message) const;

    [[nodiscard]] char* File() const;
    [[nodiscard]] size_t FileLen() const;
    _ZENER_SHORT_FUNC int Code() const { return _code; }

  private:
    void addStateLine(Buffer& buff);
    void addHeader(Buffer& buff) const;
    void addContent(Buffer& buff);

    void errorHtml();

    [[nodiscard]] std::string getFileType() const;

    int _code;
    bool _isKeepAlive;

    std::string _path;
    std::string _staticDir;

    std::string _cachedFilePath; // 废弃的 cache

    char* _file;
    struct stat _fileStat{};

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;
    static const std::unordered_map<int, std::string> CODE_STATUS;
    static const std::unordered_map<int, std::string> CODE_PATH;
};

// enum class StatusCode {
//     HTTP_OK = 200,
//     HTTP_CREATED = 201,
//     HTTP_BAD_REQUEST = 400,
//     HTTP_NOT_FOUND = 404,
//     HTTP_INTERNAL_SERVER_ERROR = 500,
// };

// const char* StatusCodeToString(StatusCode code);

// class Response {
//   public:
//     Response() {}
//     ~Response() {}

//     Response& Status(int code);
//     Response& SetHeader(const std::string& key, const std::string& vaule);
//     void Send(const std::string& content);
//     // void Json(const Json& data);
//     void SendFile(const std::string& path);

//   private:
//     StatusCode code;
// };

} // namespace zener::http

#endif // !ZENER_HTTP_RESPONSE_H