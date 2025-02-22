#ifndef ZENER_HTTP_RESPONSE_H
#define ZENER_HTTP_RESPONSE_H

#include "buffer/buffer.h"

#include <string>
#include <unordered_map>

namespace zws {
namespace http {

class HttpResponse {
  public:
    HttpResponse();
    ~HttpResponse();

    void Init(const std::string& srcDir, std::string& path,
              bool isKeepAlive = false, int code = -1);
    void MakeResponse(Buffer& buff);
    void UnmapFile();
    char* File();
    size_t FileLen() const;
    void ErrorContent(Buffer& buff, std::string message);
    int Code() const { return code_; }

  private:
    void AddStateLine_(Buffer& buff);
    void AddHeader_(Buffer& buff);
    void AddContent_(Buffer& buff);

    void ErrorHtml_();
    std::string GetFileType_();

    int code_;
    bool isKeepAlive_;

    std::string path_;
    std::string srcDir_;

    char* mmFile_;
    struct stat mmFileStat_;

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

} // namespace http
} // namespace zws

#endif // !ZENER_HTTP_RESPONSE_H