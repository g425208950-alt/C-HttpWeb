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
#include <vector>

class TCPServer
{
public:
    using ConnectionHandler = std::function<void(int)>; // handler receives accepted client fd

    explicit TCPServer(int port)
        : port_(port), listen_fd_(-1), epoll_fd_(-1), running_(false)
    {
    }

    ~TCPServer()
    {
        Stop();
    }

    void SetConnectionHandler(ConnectionHandler handler)
    {
        handler_ = std::move(handler);
    }

    bool Start(int backlog = 10)
    {
        if (running_ == true)
        {
            return false;
        }

        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0)
        {
            return false;
        }

        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, static_cast<const void *>(&opt), sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port_));
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        if (listen(listen_fd_, backlog) < 0)
        {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        if (!MakeNonBlocking(listen_fd_))
        {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ < 0)
        {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
        epoll_event event{};
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = listen_fd_;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event) < 0)
        {
            ::close(listen_fd_);
            ::close(epoll_fd_);
            listen_fd_ = -1;
            epoll_fd_ = -1;
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

        if (event_thread_.joinable())
            event_thread_.join();
    }

private:
    static bool MakeNonBlocking(const int fd)
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0)
            return false;
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
    }

    void EventLoop()
    {
        constexpr int kMaxEvents = 64; // 可以适当调大
        std::vector<epoll_event> events(kMaxEvents);

        while (running_)
        {
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
                uint32_t revents = events[i].events;

                // 1. 处理新连接接入 (listen_fd)
                if (fd == listen_fd_)
                {
                    while (true)
                    {
                        sockaddr_in client_addr{};
                        socklen_t client_len = sizeof(client_addr);
                        int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_addr), &client_len);

                        if (client_fd < 0)
                        {
                            if (errno == EAGAIN || errno == EWOULDBLOCK)
                                break; // 连干净了
                            if (errno == EINTR)
                                continue;
                            break;
                        }

                        if (!MakeNonBlocking(client_fd))
                        {
                            ::close(client_fd);
                            continue;
                        }

                        // 🔥 【修复 Bug】：必须把新连进来的 client_fd 注册到 epoll 中！
                        epoll_event ev{};
                        ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT; // 加上 ONESHOT 防止多线程竞争
                        ev.data.fd = client_fd;
                        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev) < 0)
                        {
                            ::close(client_fd);
                        }
                    }
                }
                // 2. 处理已连接客户端的数据读写事件
                else if (revents & EPOLLIN)
                {
                    // 局部拷贝一份 handler，安全起见
                    ConnectionHandler h = handler_;
                    if (h)
                    {
                        // 🔥 【优化隐患】：直接在当前线程同步处理，或者扔进你现有的线程池中。
                        // 绝对不要每次都 std::thread(...).detach();
                        h(fd);

                        // 注意：如果用了 EPOLLONESHOT，处理完业务后需要用 epoll_ctl 重置一下 fd，
                        // 如果业务处理完了需要关闭连接，直接 close(fd) 即可，内核会自动将其从 epoll 中移除。
                        ::close(fd);
                    }
                    else
                    {
                        ::close(fd);
                    }
                }
                // 3. 错误处理
                else if (revents & (EPOLLERR | EPOLLHUP))
                {
                    ::close(fd);
                }
            }
        }
    }

    int port_;                  // 端口号
    int listen_fd_;             // 监听套接字文件描述符
    int epoll_fd_;              // epoll 实例文件描述符
    std::thread event_thread_;  // epoll 事件循环线程
    std::atomic<bool> running_; // 服务器运行状态
    ConnectionHandler handler_; // 连接处理器
};
