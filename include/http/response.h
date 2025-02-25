#ifndef ZENER_HTTP_RESPONSE_H
#define ZENER_HTTP_RESPONSE_H

#include "buffer/buffer.h"

#include <string>
#include <unordered_map>


namespace zener::http {

class Response {
  public:
    Response();
    ~Response();

    void Init(const std::string& srcDir, const std::string& path,
              bool isKeepAlive = false, int code = -1);
    void MakeResponse(Buffer& buff);
    void UnmapFile();
    void ErrorContent(Buffer& buff, const std::string& message) const;

    [[nodiscard]] char* File() const;
    [[nodiscard]] size_t FileLen() const;
    [[nodiscard]] int Code() const { return _code; }

  private:
    void addStateLine(Buffer& buff);
    void addHeader(Buffer& buff) const;
    void addContent(Buffer& buff);

    void errorHtml();

    [[nodiscard]] std::string getFileType() const;

    int _code;
    bool _isKeepAlive;
    std::string _path;
    std::string _srcDir;
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