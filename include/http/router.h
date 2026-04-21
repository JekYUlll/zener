#ifndef ZENER_HTTP_ROUTER_H
#define ZENER_HTTP_ROUTER_H

#include "http/context.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace zener::http {

using HandlerFunc = std::function<void(Context&)>;

struct DispatchResult {
    enum class Kind { None, Handler, StaticFile };
    Kind kind{Kind::None};
    std::string fsRoot;       // only set for StaticFile
    std::string relativePath; // only set for StaticFile
};

class Router {
  public:
    void Add(const std::string& method, const std::string& path, HandlerFunc handler) {
        _routes[routeKey(method, path)] = std::move(handler);
    }

    // Mount a filesystem directory at a URL prefix.
    // e.g. Static("/static", "./static") serves GET /static/foo.js from ./static/foo.js
    void Static(const std::string& urlPrefix, const std::string& fsRoot) {
        _staticMounts.push_back({urlPrefix, fsRoot});
    }

    DispatchResult Dispatch(Context& ctx) const {
        // 1. exact route match
        auto it = _routes.find(routeKey(ctx.Method(), ctx.Path()));
        if (it != _routes.end()) {
            it->second(ctx);
            return {DispatchResult::Kind::Handler, {}, {}};
        }

        // 2. static prefix match (GET only)
        if (ctx.Method() == "GET") {
            const std::string& path = ctx.Path();
            for (const auto& [prefix, fsRoot] : _staticMounts) {
                if (path.size() >= prefix.size() &&
                    path.compare(0, prefix.size(), prefix) == 0) {
                    // strip prefix, keep leading slash
                    std::string rel = path.substr(prefix.size());
                    if (rel.empty() || rel[0] != '/') rel = '/' + rel;
                    // basic path traversal guard
                    if (rel.find("..") != std::string::npos)
                        return {};
                    return {DispatchResult::Kind::StaticFile, fsRoot, rel};
                }
            }
        }

        return {};
    }

  private:
    static std::string routeKey(const std::string& method, const std::string& path) {
        return method + ':' + path;
    }

    std::unordered_map<std::string, HandlerFunc> _routes;

    struct Mount { std::string prefix; std::string fsRoot; };
    std::vector<Mount> _staticMounts;
};

} // namespace zener::http

#endif // !ZENER_HTTP_ROUTER_H
