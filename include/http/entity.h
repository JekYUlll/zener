#ifndef ZENER_HTTP_ENTITY
#define ZENER_HTTP_ENTITY

// TODO 优化 http 解析

#include <vector>

namespace zener {
namespace http {

#define CR 0x0D
#define LF 0x0A



// Chunked
struct Message {
    // 8 位组字节流
    char octet[8];
};

class Entity {
  public:
    std::vector<Message> messages;
};

} // namespace http
} // namespace zener

#endif // !ZENER_HTTP_ENTITY
