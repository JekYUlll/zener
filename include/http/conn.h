#ifndef ZENER_HTTP_CONN_H
#define ZENER_HTTP_CONN_H
// 工作线程预先调用Read()从socket缓冲区读入报文进读缓冲，接着调用Parse()先解析读缓冲的请求报文，然后根据其内容制作应答报文并写入写缓冲，
// 最后将分散写指针设置在写缓冲的相应位置，方便工作线程调用Write()写出至socket缓冲区。

// 工作线程的 task
// 一个工作线程负责调用一个 connector 来处理一条连接

// TODO 连接复用

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
    // Process 函数返回的状态
    enum class ProcessResult {
        NEED_MORE_DATA, // 需要更多数据（继续等待EPOLLIN）
        RETRY_LATER,    // 写操作需重试（注册EPOLLOUT）
        OK,             // 处理成功（正常流转）
        ERROR           // 严重错误（需关闭连接）
    };

    Conn();
    ~Conn();
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    Conn(Conn&& other) noexcept;
    Conn& operator=(Conn&& other) noexcept;

    void Init(int sockFd, const sockaddr_in& addr);

    void SetConnId(const uint64_t id) { _connId = id; }
    _ZENER_SHORT_FUNC uint64_t GetConnId() const { return _connId; }

    void Close();

    _ZENER_SHORT_FUNC bool IsClosed() const { return _isClose; }

    [[nodiscard]] ssize_t Read(int* saveErrno);

    [[nodiscard]] ssize_t Write(int* saveErrno);

    [[nodiscard]] ProcessResult Process();

    // 需要写出的字节数
    _ZENER_SHORT_FUNC size_t ToWriteBytes() const {
        return _iov[0].iov_len + _iov[1].iov_len;
    }

    _ZENER_SHORT_FUNC int GetFd() const { return _fd; }

    _ZENER_SHORT_FUNC uint16_t GetPort() const { return _addr.sin_port; }

    _ZENER_SHORT_FUNC const char* GetIP() const {
        return inet_ntoa(_addr.sin_addr);
    }

    _ZENER_SHORT_FUNC sockaddr_in GetAddr() const { return _addr; }

    _ZENER_SHORT_FUNC bool IsKeepAlive() const {
        return _request.IsKeepAlive();
    }

    static bool isET;             // 是否为边缘触发
    static const char* staticDir; // 请求文件对应的根目录
    static std::atomic<int>
        userCount; // TODO 感觉这玩意应该放在 Server 里，就不需要用原子了

  private:
    int _fd;
    struct sockaddr_in _addr{};
    uint64_t _connId{
        0}; // 连接唯一标识符 0为非法值 // TODO 需要把外层 ConnInfo 的设置进来
    /*
        尝试改为原子，但实际上有点破坏 conn 的 "值属性"
        不仅代表是否关闭，也代表是否完成 init
    */
    bool _isClose{}; //

    int _iovCnt{}; // TODO 检查赋值，是否用到？
    struct iovec _iov[2]{};

    Buffer _readBuff;  // 读缓冲区
    Buffer _writeBuff; // 写缓冲区

    Request _request;
    Response _response;
};

} // namespace zener::http

#endif // !ZENER_HTTP_CONN_H