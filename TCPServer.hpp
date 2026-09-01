#pragma once

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <functional>
#include <thread>
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>

#include "ServerConfig.hpp"

// 单线程 Reactor（ET）：一个 epoll + 一个事件循环线程。
//   - listen_fd 与所有 client_fd 都注册在 epoll 上（EPOLLET 边沿触发）。
//   - 所有 I/O 非阻塞：accept / recv / send 都必须循环 drain 到 EAGAIN。
//   - 每条连接的 I/O 状态（收/发缓冲、keep-alive、空闲时间戳等）存在堆上 Connection 里，
//     不再有 worker 线程独占一条连接的完整生命周期。
//   - 业务层通过 SetOnReadable 注入一个回调：当某连接收到数据后，循环线程把该 Connection
//     交给回调，回调负责解析 HTTP、跑路由、把响应序列化塞进 conn.send_buf；循环线程随后负责写出。
class TCPServer
{
public:
    // 单条连接的 I/O 状态（不含任何 HTTP 类型，保持 TCPServer 与协议解耦）
    struct Connection
    {
        int fd = -1;
        std::string recv_buf;                       // 已读入但尚未被业务层消费的字节流
        std::string send_buf;                       // 待发送的字节流
        size_t send_offset = 0;                     // send_buf 中已发送的字节数
        bool keep_alive = true;                     // 是否还能继续处理下一个请求
        bool closing = false;                       // 写完当前响应后即关闭连接
        bool epollout_registered = false;           // 当前是否已注册 EPOLLOUT
        std::chrono::steady_clock::time_point last_active;  // 最近一次有 I/O 活动的时间
        std::chrono::steady_clock::time_point request_start; // 当前在途请求首字节到达时间（防 Slowloris）
    };

    // 业务层回调：收到数据后处理 recv_buf，可能向 send_buf 追加响应
    using OnReadable = std::function<void(Connection &)>;

    explicit TCPServer(const ServerConfig &config)
        : config_(config)
    {
    }

    ~TCPServer() { Stop(); }

    void SetOnReadable(OnReadable cb)
    {
        on_readable_ = std::move(cb);
    }

    bool Start()
    {
        if (running_)
            return false;

        if (!CreateListenSocket() || !SetupEpoll())
        {
            CloseListenAndEpoll();
            return false;
        }

        running_ = true;
        event_thread_ = std::thread(&TCPServer::EventLoop, this);
        return true;
    }

    void Stop()
    {
        if (!running_)
            return;
        running_ = false;

        CloseListenAndEpoll();

        if (event_thread_.joinable())
            event_thread_.join();

        // 关闭所有尚未结束的客户端连接
        for (auto &p : connections_)
        {
            if (p.second.fd >= 0)
                ::close(p.second.fd);
        }
        connections_.clear();
    }

private:
    static bool MakeNonBlocking(int fd)
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0)
            return false;
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
    }

    // 创建监听套接字：socket -> SO_REUSEADDR -> bind -> listen -> 非阻塞
    bool CreateListenSocket()
    {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0)
            return false;

        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(config_.port));
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
            return false;

        if (listen(listen_fd_, config_.backlog) < 0)
            return false;

        return MakeNonBlocking(listen_fd_);
    }

    // 创建 epoll 实例，并把 listen_fd 以 ET 模式挂上去
    bool SetupEpoll()
    {
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ < 0)
            return false;

        epoll_event event{};
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = listen_fd_;
        return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event) == 0;
    }

    // 统一关闭并复位 listen_fd / epoll_fd（幂等）
    void CloseListenAndEpoll()
    {
        if (epoll_fd_ >= 0)
        {
            ::close(epoll_fd_);
            epoll_fd_ = -1;
        }
        if (listen_fd_ >= 0)
        {
            shutdown(listen_fd_, SHUT_RDWR);
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
    }

    // 关闭一条客户端连接：从 epoll 摘除 -> close -> 从连接表移除
    void CloseConnection(int fd)
    {
        if (fd < 0)
            return;
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        ::close(fd);
        connections_.erase(fd);
    }

    // 事件循环：listen 上的 EPOLLIN -> accept；client 上的可读/可写/错误 -> 对应处理
    void EventLoop()
    {
        constexpr int kMaxEvents = 128;
        std::vector<epoll_event> events(kMaxEvents);

        while (running_)
        {
            // 用较短超时定期返回，以便扫描空闲连接
            int n = epoll_wait(epoll_fd_, events.data(), kMaxEvents, 1000);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                break;
            }

            for (int i = 0; i < n; ++i)
            {
                int fd = events[i].data.fd;
                uint32_t ev = events[i].events;

                if (fd == listen_fd_)
                {
                    HandleAccept();
                    continue;
                }

                // 对端关闭 / 出错：优先处理，避免漏关
                if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
                {
                    CloseConnection(fd);
                    continue;
                }

                if (ev & EPOLLIN)
                    HandleRead(fd);

                // 读处理可能已关闭连接（对端 FIN / 协议错误），需再查一次
                if (connections_.find(fd) == connections_.end())
                    continue;

                if (ev & EPOLLOUT)
                    HandleWrite(fd);
            }

            SweepTimers();
        }
    }

    // ET 模式必须循环 accept 到 EAGAIN/EWOULDBLOCK，把所有 pending 连接一次取干净
    void HandleAccept()
    {
        while (true)
        {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_addr), &client_len);

            if (client_fd < 0)
            {
                if (errno == EINTR)
                    continue;
                break; // EAGAIN/EWOULDBLOCK 表示已取干净，其他错误也结束本轮
            }

            if (!MakeNonBlocking(client_fd))
            {
                ::close(client_fd);
                continue;
            }

            Connection &conn = connections_[client_fd];
            conn.fd = client_fd;
            conn.last_active = std::chrono::steady_clock::now();
            conn.request_start = conn.last_active; // 占位初值，真正计时从首个请求字节到达起

            epoll_event event{};
            event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
            event.data.fd = client_fd;
            if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event) < 0)
            {
                ::close(client_fd);
                connections_.erase(client_fd);
            }
        }
    }

    // client 可读：ET 必须 drain 到 EAGAIN；收完交给业务层解析+路由+填 send_buf
    void HandleRead(int fd)
    {
        auto it = connections_.find(fd);
        if (it == connections_.end())
            return;
        Connection &conn = it->second;
        auto now = std::chrono::steady_clock::now();
        conn.last_active = now;

        char buffer[4096];
        bool peer_closed = false;
        bool hard_error = false;

        // ET：一次性读干净
        while (true)
        {
            ssize_t r = recv(fd, buffer, sizeof(buffer), 0);
            if (r > 0)
            {
                // 缓冲从空到非空 = 新请求的首字节到达，从这里起算请求总时限；
                // 残留的半截请求（pipelining 未收完）不会重置，deadline 始终从真实起点算
                if (conn.recv_buf.empty())
                    conn.request_start = now;
                conn.recv_buf.append(buffer, static_cast<size_t>(r));
                continue;
            }
            if (r == 0)
            {
                peer_closed = true; // 对端 FIN
                break;
            }
            // r < 0
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break; // 读干净了
            hard_error = true; // 真实错误
            break;
        }

        // 交给业务层：解析 recv_buf 中的完整请求并填 send_buf（支持 pipelining）
        if (!conn.recv_buf.empty() && on_readable_ && !conn.closing)
            on_readable_(conn);

        // 写侧背压：客户端读得太慢导致待发响应积压超限，直接关闭，防止 send_buf 无界增长
        if (conn.send_buf.size() - conn.send_offset > config_.max_pending_write)
        {
            CloseConnection(fd);
            return;
        }

        // 传输层兜底：业务层消费后仍有超过「头部上限+body上限」的残留，属协议层也没拦住的畸形数据，防御性关闭
        if (!conn.closing && conn.recv_buf.size() > config_.max_header_bytes + config_.max_body_bytes)
        {
            CloseConnection(fd);
            return;
        }

        // 业务层可能标记 closing（协议错误 / Connection: close），但还要等写完响应再关
        if (hard_error)
        {
            CloseConnection(fd);
            return;
        }

        // 对端关了：把已有响应发完再关；若已无待发，立即关
        if (peer_closed)
        {
            if (conn.send_offset < conn.send_buf.size())
            {
                conn.closing = true;
                TryRegisterEpollOut(conn);
            }
            else
            {
                CloseConnection(fd);
            }
            return;
        }

        // 正常情况：若有待发数据，确保 EPOLLOUT 已注册
        if (conn.send_offset < conn.send_buf.size())
            TryRegisterEpollOut(conn);
    }

    // client 可写：ET 必须 drain 到 EAGAIN；发完摘 EPOLLOUT，closing 则关连接
    void HandleWrite(int fd)
    {
        auto it = connections_.find(fd);
        if (it == connections_.end())
            return;
        Connection &conn = it->second;

        while (conn.send_offset < conn.send_buf.size())
        {
            ssize_t w = send(fd, conn.send_buf.data() + conn.send_offset,
                             conn.send_buf.size() - conn.send_offset, 0);
            if (w > 0)
            {
                conn.send_offset += static_cast<size_t>(w);
                continue;
            }
            if (w < 0 && errno == EINTR)
                continue;
            if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break; // 写缓冲满，等下次 EPOLLOUT
            // 真实错误
            CloseConnection(fd);
            return;
        }

        // 本批响应已全部发完
        if (conn.send_offset >= conn.send_buf.size())
        {
            conn.send_buf.clear();
            conn.send_offset = 0;
            UnregisterEpollOut(conn);

            if (conn.closing || !conn.keep_alive)
            {
                CloseConnection(fd);
                return;
            }
        }
        else
        {
            // 兜底：EAGAIN 卡住且待发量超限（读侧检查之外的另一道闸），关闭
            if (conn.send_buf.size() - conn.send_offset > config_.max_pending_write)
            {
                CloseConnection(fd);
                return;
            }
            // 还有未发数据，确保 EPOLLOUT 已注册
            TryRegisterEpollOut(conn);
        }
    }

    // 注册 EPOLLOUT（若尚未注册），让下次可写时继续发
    void TryRegisterEpollOut(Connection &conn)
    {
        if (conn.epollout_registered || conn.fd < 0)
            return;
        epoll_event event{};
        event.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
        event.data.fd = conn.fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn.fd, &event) == 0)
            conn.epollout_registered = true;
    }

    // 摘掉 EPOLLOUT，回到只读等下一条请求
    void UnregisterEpollOut(Connection &conn)
    {
        if (!conn.epollout_registered || conn.fd < 0)
            return;
        epoll_event event{};
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        event.data.fd = conn.fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn.fd, &event) == 0)
            conn.epollout_registered = false;
    }

    // 粗粒度定时扫描（替代原来 per-连接 poll(timeout) 阻塞等待），双规则：
    //   规则1：无在途请求（recv_buf 空）→ 按 last_active 算 keep-alive 空闲超时
    //   规则2：有在途请求（recv_buf 非空）→ 按 request_start 算请求总时限。
    //          Slowloris 每隔几秒滴 1 个字节能不断刷新 last_active，但刷不动 request_start，
    //          请求永远凑不齐也会在 deadline 处被杀
    void SweepTimers()
    {
        auto now = std::chrono::steady_clock::now();
        auto idle_timeout = std::chrono::milliseconds(config_.read_timeout_ms);
        auto request_deadline = std::chrono::milliseconds(config_.request_deadline_ms);

        // 收集要关的 fd，避免边遍历边 erase
        std::vector<int> to_close;
        for (auto &p : connections_)
        {
            const Connection &conn = p.second;
            if (!conn.recv_buf.empty())
            {
                if (now - conn.request_start > request_deadline)
                    to_close.push_back(p.first);
            }
            else if (now - conn.last_active > idle_timeout)
            {
                to_close.push_back(p.first);
            }
        }
        for (int fd : to_close)
            CloseConnection(fd);
    }

    ServerConfig config_;               // 服务器配置
    int listen_fd_ = -1;                // 监听套接字文件描述符
    int epoll_fd_ = -1;                 // epoll 实例文件描述符
    std::thread event_thread_;          // epoll 事件循环线程
    std::atomic<bool> running_{false}; // 服务器运行状态
    OnReadable on_readable_;            // 业务层回调（解析+路由+填 send_buf）
    std::unordered_map<int, Connection> connections_; // fd -> 连接状态
};
