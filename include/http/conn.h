#ifndef ZENER_HTTP_CONN_H
#define ZENER_HTTP_CONN_H

// 工作线程预先调用Read()从socket缓冲区读入报文进读缓冲，接着调用Parse()先解析读缓冲的请求报文，然后根据其内容制作应答报文并写入写缓冲，
// 最后将分散写指针设置在写缓冲的相应位置，方便工作线程调用Write()写出至socket缓冲区。

// 工作线程的 task
// 一个工作线程负责调用一个 connector 来处理一条连接

#include "buffer/buffer.h"
#include "common.h"
#include "http/request.h"
#include "http/response.h"

#include <arpa/inet.h> // sockaddr_in
#include <cstdint>     // uint64_t
#include <sys/types.h>

namespace zener::http {

// TODO
// 现在的 Conn 存储 request 和 response , 感觉有点占空间
// 可以修改为指针或者句柄

class Conn {
  public:
    Conn();
    ~Conn();
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    Conn(Conn&& other) noexcept;
    Conn& operator=(Conn&& other) noexcept;

    void Init(int sockFd, const sockaddr_in& addr);

    // 设置连接ID
    void SetConnId(const uint64_t id) { _connId = id; }

    // 获取连接ID
    _ZENER_SHORT_FUNC uint64_t GetConnId() const { return _connId; }

    void Close();

    [[nodiscard]] ssize_t Read(int* saveErrno);

    [[nodiscard]] ssize_t Write(int* saveErrno);

    [[nodiscard]] bool Process();

    // 需要写出的字节数
    _ZENER_SHORT_FUNC int ToWriteBytes() const {
        return _iov[0].iov_len + _iov[1].iov_len;
    }

    _ZENER_SHORT_FUNC int GetFd() const { return _fd; }

    _ZENER_SHORT_FUNC int GetPort() const { return _addr.sin_port; }

    _ZENER_SHORT_FUNC const char* GetIP() const {
        return inet_ntoa(_addr.sin_addr);
    }

    _ZENER_SHORT_FUNC sockaddr_in GetAddr() const { return _addr; }

    _ZENER_SHORT_FUNC bool IsKeepAlive() const {
        return _request.IsKeepAlive();
    }

    static bool isET;             // 是否为边缘触发
    static const char* staticDir; // 请求文件对应的根目录
    static std::atomic<int> userCount; // TODO 感觉这玩意应该放在 Server 里，就不需要用原子了

  private:
    int _fd;
    struct sockaddr_in _addr{};
    uint64_t _connId; // 连接唯一标识符
    bool _isClose{};

    int _iovCnt{};
    struct iovec _iov[2]{};

    Buffer _readBuff;  // 读缓冲区
    Buffer _writeBuff; // 写缓冲区

    Request _request;
    Response _response;
};

} // namespace zener::http

#endif // !ZENER_HTTP_CONN_H