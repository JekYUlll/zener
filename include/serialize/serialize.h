#ifndef ZENER_SERIALIZE_H
#define ZENER_SERIALIZE_H

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace zener {

class ISerializable {
  public:
    virtual ~ISerializable() = default;

    virtual std::string ToString() { return "default"; }
    virtual nlohmann::json ToJson() = 0;

    virtual bool FromString(const std::string_view &str) { return false; }
    virtual bool FromJson(const nlohmann::json &json) = 0;
};

} // namespace zener

#endif // !ZENER_SERIALIZE_H