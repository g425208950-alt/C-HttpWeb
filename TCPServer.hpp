#pragma once

#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <functional>
#include <thread>
#include <atomic>
#include <string>

class TCPServer {
public:
    using ConnectionHandler = std::function<void(int)>; // handler receives accepted client fd

    // Constructor takes the port number to listen on.
    explicit TCPServer(int port)
        : port_(port), listen_fd_(-1), running_(false)
    {}

    // Destructor ensures the server is stopped and resources are cleaned up.
    ~TCPServer()
    {
        Stop();
    }

    // Set the connection handler that will be invoked for each accepted connection.
    void SetConnectionHandler(ConnectionHandler handler)
    {
        handler_ = std::move(handler);
    }

    // Start listening and accepting connections. Returns true on success.
    bool Start(int backlog = 10)
    {
        if (running_) return false;

        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) return false;

        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(static_cast<uint16_t>(port_));

        if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        if (listen(listen_fd_, backlog) < 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        running_ = true;
        accept_thread_ = std::thread(&TCPServer::AcceptLoop, this);
        return true;
    }

    // Stop the server and join background threads.
    void Stop()
    {
        if (!running_) return;
        running_ = false;

        if (listen_fd_ >= 0) {
            shutdown(listen_fd_, SHUT_RDWR);
            ::close(listen_fd_);
            listen_fd_ = -1;
        }

        if (accept_thread_.joinable()) accept_thread_.join();
    }

private:
    void AcceptLoop()
    {
        while (running_) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd < 0) {
                if (errno == EINTR) continue;   
                if (!running_) break;
                continue;
            }

            //分析一下逻辑：当有新的连接到来时，服务器会调用handler_来处理这个连接。为了避免阻塞主线程，服务器会为每个连接创建一个新的线程来处理它。处理完成后，线程会关闭客户端的文件描述符。
            ConnectionHandler h = handler_;
            if (h) {
                std::thread([h, client_fd]() mutable {// mutable是因为传值拷贝默认const,function对象可能会因为找不到const operator()而无法调用，所以需要mutable。
                    h(client_fd); 
                    ::close(client_fd);
                }).detach();
            } else {
                ::close(client_fd);
            }
        }
    }

    
    int port_; // 端口号 
    int listen_fd_; // 监听套接字文件描述符
    std::thread accept_thread_; // 接受连接的线程
    std::atomic<bool> running_; // 服务器运行状态
    ConnectionHandler handler_; // 连接处理器
};
