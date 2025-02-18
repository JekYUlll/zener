
namespace zws {

enum class GeneralErrorCode {
    GENERAL_ERROR = -1,
    INVALID_INPUT = -2,
    FILE_NOT_FOUND = -3
};

enum class HttpStatusCode {
    HTTP_OK = 200,
    HTTP_CREATED = 201,
    HTTP_BAD_REQUEST = 400,
    HTTP_NOT_FOUND = 404,
    HTTP_INTERNAL_SERVER_ERROR = 500
};

// 将普通函数错误码转换为字符串
const char* GeneralErrorCodeToString(GeneralErrorCode code);

// 将HTTP状态码转换为字符串
const char* HttpStatusCodeToString(HttpStatusCode code);

} // namespace zws
