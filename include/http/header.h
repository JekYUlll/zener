#ifndef ZENER_HTTP_HEADER_H
#define ZENER_HTTP_HEADER_H

// TODO 优化 http 解析

namespace zener {
namespace http {

// 通用首部字段（General Header Fields）
// 请求报文和响应报文两方都会使用的首部。

// 请求首部字段（Request Header Fields）
// 从客户端向服务器端发送请求报文时使用的首部。补充了请求的附
// 加内容、客户端信息、响应内容相关优先级等信息。

// 响应首部字段（Response Header Fields）
// 从服务器端向客户端返回响应报文时使用的首部。补充了响应的附
// 加内容，也会要求客户端附加额外的内容信息。

// 实体首部字段（Entity Header Fields）
// 针对请求报文和响应报文的实体部分使用的首部。补充了资源内容
// 更新时间等与实体有关的信息。

class Header {};

class ReqHeader : public Header {};
class ResHeader : public Header {};

} // namespace http
} // namespace zener

#endif // !ZENER_HTTP_HEADER_H