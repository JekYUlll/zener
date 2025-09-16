#ifndef ZENER_SERVER_H
#define ZENER_SERVER_H
/*
    TODO 添加 /health 端点响应 200 OK，供负载均衡器检测
    示例：
    cpp
    server.AddRoute("/health", [](const Request& req, Response& res) {
        res.SetStatus(200).SetBody("OK");
    });
*/
#include "core/epoller.h"
#include "http/conn.h"
#include "task/threadpool_1.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <memory>
#include <netinet/in.h>
#include <shared_mutex>
#include <unordered_map>

namespace zener {
namespace v0 {

class Server {

  public:
    Server(int port, int trigMode, int timeoutMS, bool optLinger,
           const char *sqlHost, int sqlPort, const char *sqlUser,
           const char *sqlPwd, const char *dbName, int connPoolNum,
           int threadNum, bool openLog = false, int logLevel = -1,
           int logQueSize = -1);

    ~Server();

    void Run();
    void Stop();

    _ZENER_SHORT_FUNC bool IsClosed() const {
        return _isClose.load(std::memory_order_relaxed);
    }

  private:
    /*
        包含连接ID的连接信息结构体
        为了扩展性
    */
    struct ConnInfo {
        // http::Conn conn;   // 连接对象 TODO 改为智能指针
        std::unique_ptr<http::Conn> conn{nullptr}; // 初始化为 nullptr
        /*
            唯一连接ID 用于替代fd. 从1开始，0表示无效值.
            默认无效，防止忘了赋值
         */
        uint64_t connId{INVALID_CONN_ID};

        std::atomic<bool> active{true};

        static constexpr uint64_t INVALID_CONN_ID = 0;

        ConnInfo() = default;
        ~ConnInfo() = default;
        ConnInfo(const ConnInfo &) = delete;
        ConnInfo &operator=(const ConnInfo &) = delete;

        ConnInfo(ConnInfo &&other) noexcept
            : conn(std::move(other.conn)), connId(other.connId) {
            other.connId = 0;
            other.conn = nullptr;
        }

        ConnInfo &operator=(ConnInfo &&other) noexcept {
            if (this != &other) {
                conn = std::move(other.conn);
                connId = other.connId;
                other.connId = 0;
                other.conn = nullptr;
            }
            other.conn = {};
            other.connId = -1;
            return *this;
        }
    };

    bool initSocket();
    void initEventMode(int trigMode);
    void addClient(int fd, const sockaddr_in &addr);

    void dealListen();
    void dealRead(http::Conn *client);
    void dealWrite(http::Conn *client);

    static void sendError(int fd, const char *info);
    void extentTime(http::Conn *client); // 刷新连接的超时时间

    void closeConn(http::Conn *client); // 正常工作线程中的关闭逻辑
    void closeConnAsync(int fd, const std::function<void()> &callback =
                                    nullptr); // 异步关闭连接（非阻塞）
    void _closeConnInternal(
        http::Conn &&client) const; // 实际关闭逻辑（意图与原有 closeConn
                                    // 逻辑解耦）但实际上原本实现里没用上

    void onRead(http::Conn *client);
    void handleReadError(http::Conn *client, int err);
    void onWrite(http::Conn *client);
    void onProcess(http::Conn *client);

    /// @intro 校验 conn 的 fd 和 connId 的一致性
    /// @thread 安全
    /// 1. 检查 fd 范围合法性
    /// 2. 检查 client 是否已经关闭 IsClosed()
    /// 3. 检查 fd 是否在 _users 表中
    /// 4. 检查 connID 与表中对应是否一致
    [[nodiscard]] bool checkFdAndMatchId(const http::Conn *client) const;

    /*
     * linux默认最大文件符号大小为 1024，但可以调整
     */
    static constexpr int MAX_FD = 65536;
    static constexpr int MAX_EVENTS = 1024; // TODO unused

    [[nodiscard]] static bool
    checkServerNotFull(int fd); // 检查服务器是否已满，满了返回 false

    static int setFdNonBlock(int fd);
    /*
        设置 TCP_NODELAY，禁用Nagle算法，减少小数据包（如请求头、ACK）延迟
        大文件传输或批量数据场景可保留 Nagle 算法以提升吞吐量
        任何影响数据传输行为的选项都应该在客户端套接字上设置，而不是监听套接字
    */
    static int setNoDelay(int fd);

    int _port; // 服务器监听的端口
    bool _openLinger;
    int _timeoutMS; // @
    // @改为原子. reactor主线程为单线程，但可能会使用safeguard
    std::atomic<bool> _isClose;

    int _listenFd{};
    std::string _cwd{};       // 工作目录
    std::string _staticDir{}; // 静态资源目录

    uint32_t _listenEvent{};
    uint32_t _connEvent{};

    // webserver 11 此处存储 unique_ptr<HeapTimer>, 但我计时器是单例
    std::unique_ptr<ThreadPool> _threadpool;
    std::unique_ptr<Epoller> _epoller;
    /*
        旧版本: mutable std::unordered_map<int, http::Conn> _users;
        新版本: 使用ConnInfo结构体存储连接信息
        <fd, ConnInfo>
       在连接关闭后可能被新连接重用，导致日志、监控或调试时无法区分不同连接
    */
    std::unordered_map<int, ConnInfo> _users; // 已经全部加锁
    /*
        用于生成唯一连接ID的原子计数器 --是否没必要？reactor里server是单线程的
        若其他线程（如工作线程池）可能创建连接，需保留原子操作
    */
    std::atomic<uint64_t> _nextConnId{
        1}; // 从1开始，0表示无效。fetch_add返回的是执行前的值
    /*
        通过 eventfd 创建，用于唤醒。防止退出的时候阻塞在 epoll_wait
     */
    int _wakeupFd{};
    mutable std::shared_mutex _connMutex;
};

} // namespace v0

std::unique_ptr<v0::Server> NewServerFromConfig(const std::string &configPath);

} // namespace zener

#endif // !ZENER_SERVER_H
