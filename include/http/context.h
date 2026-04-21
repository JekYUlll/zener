#ifndef ZENER_HTTP_CONTEXT_H
#define ZENER_HTTP_CONTEXT_H

#include "buffer/buffer.h"
#include "request.h"
#include "response.h"

#include <any>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace zener::http {

class Context {
  public:
    Context(Request& req, Response& res, Buffer& writeBuff)
        : _req(req), _res(res), _writeBuff(writeBuff) {}

    // ---- Request accessors ----
    const std::string& Path() const { return _req.Path(); }
    std::string Method() const { return _req.Method(); }
    std::string GetPost(const std::string& key) const { return _req.GetPost(key); }

    // ---- Typed key-value store ----
    template <typename T>
    void Set(const std::string& key, T&& val) {
        _data[key] = std::forward<T>(val);
    }

    template <typename T>
    T& Get(const std::string& key) {
        auto it = _data.find(key);
        if (it == _data.end())
            throw std::out_of_range("Context key not found: " + key);
        return std::any_cast<T&>(it->second);
    }

    bool Has(const std::string& key) const { return _data.count(key) > 0; }

    // ---- Abort flag ----
    void Abort() { _aborted = true; }
    bool IsDone() const { return _aborted; }

    // ---- Response API ----
    Context& Status(int code) { _res.Status(code); return *this; }

    void Send(const std::string& body) {
        _res.Send(_writeBuff, body);
    }

    void Json(const std::string& json) {
        _res.Json(_writeBuff, json);
    }

    void Redirect(const std::string& location) {
        _res.Status(302);
        _res.Send(_writeBuff, "");  // empty body; Location header added below
        // Inject Location header before the blank line
        // We write a minimal redirect response directly
        _writeBuff.RetrieveAll();
        _writeBuff.Append("HTTP/1.1 302 Found\r\n");
        _writeBuff.Append("Location: " + location + "\r\n");
        _writeBuff.Append("Content-length: 0\r\n\r\n");
    }

    // Raw access for advanced use
    Response& GetResponse() { return _res; }
    const Request& GetRequest() const { return _req; }

  private:
    Request& _req;
    Response& _res;
    Buffer& _writeBuff;
    std::unordered_map<std::string, std::any> _data;
    bool _aborted{false};
};

} // namespace zener::http

#endif // !ZENER_HTTP_CONTEXT_H
