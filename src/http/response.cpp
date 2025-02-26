#include "http/response.h"
#include "http/file_cache.h"
#include "utils/log/logger.h"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>

namespace zener::http {

using namespace std;

const unordered_map<string, string> Response::SUFFIX_TYPE = {
    {".html", "text/html"},
    {".xml", "text/xml"},
    {".xhtml", "application/xhtml+xml"},
    {".txt", "text/plain"},
    {".rtf", "application/rtf"},
    {".pdf", "application/pdf"},
    {".word", "application/nsword"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".au", "audio/basic"},
    {".mpeg", "video/mpeg"},
    {".mpg", "video/mpeg"},
    {".avi", "video/x-msvideo"},
    {".gz", "application/x-gzip"},
    {".tar", "application/x-tar"},
    {".css", "text/css "},
    {".js", "text/javascript "},
};

const unordered_map<int, string> Response::CODE_STATUS = {
    {200, "OK"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
};

const unordered_map<int, string> Response::CODE_PATH = {
    {400, "/400.html"},
    {403, "/403.html"},
    {404, "/404.html"},
};

Response::Response() {
    _code = -1;
    _path = _srcDir = "";
    _isKeepAlive = false;
    _file = nullptr;
    _fileStat = {0};
    _cachedFilePath = "";
};

Response::~Response() { UnmapFile(); }

void Response::Init(const string& srcDir, const string& path,
                    const bool isKeepAlive, const int code) {
    assert(!srcDir.empty());
    if (_file) {
        UnmapFile();
    }
    _code = code;
    _isKeepAlive = isKeepAlive;
    _path = path;
    _srcDir = srcDir;
    _file = nullptr;
    _fileStat = {0};
    _cachedFilePath = "";
}

void Response::MakeResponse(Buffer& buff) {
    /* 判断请求的资源文件 */
    if (stat((_srcDir + _path).data(), &_fileStat) < 0 ||
        S_ISDIR(_fileStat.st_mode)) {
        _code = 404;
    } else if (!(_fileStat.st_mode & S_IROTH)) {
        _code = 403;
    } else if (_code == -1) {
        _code = 200;
    }
    errorHtml();
    addStateLine(buff);
    addHeader(buff);
    addContent(buff);
}

char* Response::File() const { return _file; }

size_t Response::FileLen() const { return _fileStat.st_size; }

void Response::errorHtml() {
    if (CODE_PATH.count(_code) == 1) {
        _path = CODE_PATH.find(_code)->second;
        stat((_srcDir + _path).data(), &_fileStat);
    }
}

void Response::addStateLine(Buffer& buff) {
    string status;
    if (CODE_STATUS.count(_code) == 1) {
        status = CODE_STATUS.find(_code)->second;
    } else {
        _code = 400;
        status = CODE_STATUS.find(400)->second;
    }
    buff.Append("HTTP/1.1 " + to_string(_code) + " " + status + "\r\n");
}

void Response::addHeader(Buffer& buff) const {
    buff.Append("Connection: ");
    if (_isKeepAlive) {
        buff.Append("keep-alive\r\n");
        buff.Append("keep-alive: max=6, timeout=120\r\n");
    } else {
        buff.Append("close\r\n");
    }
    buff.Append("Content-type: " + getFileType() + "\r\n");
}

void Response::addContent(Buffer& buff) {
    const string fullPath = _srcDir + _path;

    // 打开文件前先记录当前路径，以便后续释放
    _cachedFilePath = fullPath;

    LOG_D("File path: {}, size: {}", fullPath.c_str(), _fileStat.st_size);

    if (_fileStat.st_size <= 0) {
        LOG_W("File size is zero or negative: {}", fullPath.c_str());
        buff.Append("Content-length: 0\r\n\r\n");
        return;
    }

    // 使用文件缓存获取文件映射
    auto cachedFile =
        FileCache::GetInstance().GetFileMapping(fullPath, _fileStat);
    if (!cachedFile) {
        LOG_E("Failed to get file mapping: {}", fullPath.c_str());
        ErrorContent(buff, "File NotFound!");
        return;
    }

    // 设置文件指针和大小
    _file = cachedFile->data;

    buff.Append("Content-length: " + to_string(_fileStat.st_size) + "\r\n\r\n");

    LOG_D("File successfully mapped to memory: address={:p}, size={}, using "
          "cache: {}",
          (void*)_file, _fileStat.st_size, !_cachedFilePath.empty());
}

void Response::UnmapFile() {
    if (_file) {
        // 记录当前文件信息用于日志
        void* filePtr = _file;
        std::string cachedPath = _cachedFilePath;

        if (!_cachedFilePath.empty()) {
            // 不直接调用munmap，而是通过缓存系统释放引用
            try {
                LOG_D("Releasing file mapping: file={}, address={:p}",
                      _cachedFilePath, (void*)_file);
                FileCache::GetInstance().ReleaseFileMapping(_cachedFilePath);
            } catch (const std::exception& e) {
                LOG_E("Exception when releasing file mapping: {}, file={}",
                      e.what(), _cachedFilePath);
            }
        } else {
            LOG_W("Trying to release invalid file mapping, address={:p}",
                  (void*)_file);
        }

        // 重置状态
        _file = nullptr;
        _cachedFilePath = "";
    }
}

string Response::getFileType() const {
    /* 判断文件类型 */
    const string::size_type idx = _path.find_last_of('.');
    if (idx == string::npos) {
        return "text/plain";
    }
    if (const string suffix = _path.substr(idx);
        SUFFIX_TYPE.count(suffix) == 1) {
        return SUFFIX_TYPE.find(suffix)->second;
    }
    return "text/plain";
}

void Response::ErrorContent(Buffer& buff, const string& message) const {
    string body;
    string status;
    body += "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    if (CODE_STATUS.count(_code) == 1) {
        status = CODE_STATUS.find(_code)->second;
    } else {
        status = "Bad Request";
    }
    body += to_string(_code) + " : " + status + "\n";
    body += "<p>" + message + "</p>";
    body += "<hr><em>TinyWebServer</em></body></html>";

    buff.Append("Content-length: " + to_string(body.size()) + "\r\n\r\n");
    buff.Append(body);
}

// const char* StatusCodeToString(StatusCode code) {
//     switch (code) {
//     case StatusCode::HTTP_OK:
//         return "HTTP 200 OK";
//     case StatusCode::HTTP_CREATED:
//         return "HTTP 201 Created";
//     case StatusCode::HTTP_BAD_REQUEST:
//         return "HTTP 400 Bad Request";
//     case StatusCode::HTTP_NOT_FOUND:
//         return "HTTP 404 Not Found";
//     case StatusCode::HTTP_INTERNAL_SERVER_ERROR:
//         return "HTTP 500 Internal Server Error";
//     default:
//         return "Unknown Error";
//     }
// }

} // namespace zener::http
