#ifndef ZENER_HTTP_CONTEXT_H
#define ZENER_HTTP_CONTEXT_H

// TODO

#include "request.h"
#include "response.h"

#include <any>
#include <chrono>
#include <future>

namespace zener {
namespace http {

class Context {
  public:
    Context(Request& req, Response& res);

    void SetTimeout(int milliseconds);

    void Cancle();

  private:
    Request& _req;
    Response& _res;
    std::unordered_map<std::string, std::any> _data;
    std::chrono::steady_clock::time_point _deadline;
    std::atomic<bool> _cancelled;
    std::future<void> _timeout_future;
    // NextHandler next_handler_;
};

} // namespace http
} // namespace zener

#endif // !ZENER_HTTP_CONTEXT_H