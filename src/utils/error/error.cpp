#include "utils/error/error.h"

namespace zws {
namespace error {

const char* GeneralErrorCodeToString(GeneralErrorCode code) {
    switch (code) {
    case GeneralErrorCode::GENERAL_ERROR:
        return "General Error";
    case GeneralErrorCode::INVALID_INPUT:
        return "Invalid Input";
    case GeneralErrorCode::FILE_NOT_FOUND:
        return "File Not Found";
    default:
        return "Unknown Error";
    }
}

} // namespace error
} // namespace zws
