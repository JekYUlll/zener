#include "http/response.h"

namespace zws {
namespace http {

const char* StatusCodeToString(StatusCode code) {
    switch (code) {
    case StatusCode::HTTP_OK:
        return "HTTP 200 OK";
    case StatusCode::HTTP_CREATED:
        return "HTTP 201 Created";
    case StatusCode::HTTP_BAD_REQUEST:
        return "HTTP 400 Bad Request";
    case StatusCode::HTTP_NOT_FOUND:
        return "HTTP 404 Not Found";
    case StatusCode::HTTP_INTERNAL_SERVER_ERROR:
        return "HTTP 500 Internal Server Error";
    default:
        return "Unknown Error";
    }
}

} // namespace http
} // namespace zws