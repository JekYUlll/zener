#ifndef ZENER_HTTP_CONN_H
#define ZENER_HTTP_CONN_H

// 工作线程预先调用Read()从socket缓冲区读入报文进读缓冲，接着调用Parse()先解析读缓冲的请求报文，然后根据其内容制作应答报文并写入写缓冲，
// 最后将分散写指针设置在写缓冲的相应位置，方便工作线程调用Write()写出至socket缓冲区。

#include "buffer/buffer.h"
#include "common.h"
#include "http/request.h"
#include "http/response.h"

#include <arpa/inet.h> // sockaddr_in
#include <cstdlib>    // atoi()
#include <sys/types.h>
#include <sys/uio.h> // readv/writev


namespace zener::http {

// TODO
// 现在的 Conn 存储 request 和 response , 感觉有点占空间
// 可以修改为指针或者句柄

class Conn {
  public:
    Conn();
    ~Conn();

    void init(int sockFd, const sockaddr_in& addr);

    void Close();

    ssize_t read(int* saveErrno);

    ssize_t write(int* saveErrno);

    bool process();

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

    static bool isET;          // 是否为边缘触发
    static const char* staticDir; // 请求文件对应的根目录
    static std::atomic<int> userCount;

  private:
    int _fd;
    struct sockaddr_in _addr;

    bool _isClose;

    int _iovCnt;
    struct iovec _iov[2];

    Buffer _readBuff;  // 读缓冲区
    Buffer _writeBuff; // 写缓冲区

    Request _request;
    Response _response;
};

} // namespace zener::http


#endif // !ZENER_HTTP_CONN_H