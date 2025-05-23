#ifndef ZENER_UTILS_ERROR_H
#define ZENER_UTILS_ERROR_H
#include <stdexcept>

namespace zener::error {

enum class GeneralErrorCode {
    GENERAL_ERROR = -1,
    INVALID_INPUT = -2,
    FILE_NOT_FOUND = -3,
};

const char* GeneralErrorCodeToString(GeneralErrorCode code); // 将普通函数错误码转换为字符串

} // namespace zener::error

#endif // !ZENER_UTILS_ERROR_H
