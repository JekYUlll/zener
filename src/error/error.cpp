#include "error/error.h"

namespace zws {

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

const char* HttpStatusCodeToString(HttpStatusCode code) {
    switch (code) {
    case HttpStatusCode::HTTP_OK:
        return "HTTP 200 OK";
    case HttpStatusCode::HTTP_CREATED:
        return "HTTP 201 Created";
    case HttpStatusCode::HTTP_BAD_REQUEST:
        return "HTTP 400 Bad Request";
    case HttpStatusCode::HTTP_NOT_FOUND:
        return "HTTP 404 Not Found";
    case HttpStatusCode::HTTP_INTERNAL_SERVER_ERROR:
        return "HTTP 500 Internal Server Error";
    default:
        return "Unknown Error";
    }
}

} // namespace zws
