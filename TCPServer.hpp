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

#include "ThreadPool.hpp"
#include "ServerConfig.hpp"

class TCPServer
{
public:
    using ConnectionHandler = std::function<void(int)>; // handler receives accepted client fd

    explicit TCPServer(const ServerConfig &config)
        : config_(config), listen_fd_(-1), epoll_fd_(-1), running_(false), pool_(config.worker_threads)
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

    bool Start()
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
        addr.sin_port = htons(static_cast<uint16_t>(config_.port));
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        if (listen(listen_fd_, config_.backlog) < 0)
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

        // 等待所有正在处理连接的 worker 线程收尾（连接自然结束或读超时后才会退出）
        pool_.Stop();
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

                        // 不再把 client_fd 挂进 epoll：交给线程池里的一个 worker 独占处理，
                        // worker 用阻塞式（poll+recv）逻辑跑完这条连接的整个生命周期（可能是很多个
                        // keep-alive 请求），accept 所在的这个事件循环线程绝不会被任何一条连接卡住。
                        ConnectionHandler h = handler_;
                        pool_.Enqueue([h, client_fd]()
                                       {
                            if (h)
                            {
                                h(client_fd);
                            }
                            ::close(client_fd); });
                    }
                }
                // listen_fd_ 之外不会再收到别的事件（client_fd 已不注册进 epoll_fd_）
            }
        }
    }

    ServerConfig config_;       // 服务器配置
    int listen_fd_;             // 监听套接字文件描述符
    int epoll_fd_;              // epoll 实例文件描述符
    std::thread event_thread_;  // epoll 事件循环线程
    std::atomic<bool> running_; // 服务器运行状态
    ConnectionHandler handler_; // 连接处理器
    ThreadPool pool_;           // 处理连接的工作线程池
};
