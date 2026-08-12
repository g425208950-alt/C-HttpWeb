#pragma once

#include "TCPServer.hpp"
#include "HTTP.hpp"
#include "Router.hpp"
#include "ServerConfig.hpp"
#include <functional>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <poll.h>
#include <cerrno>

namespace http
{
    class HTTPServer
    {
    public:
        using RequestHandler = std::function<Response(const Request &)>;

        explicit HTTPServer(const ServerConfig &config)
            : config_(config), server_(config)
        {
            // 在HTTPServer的构造函数中，我们设置了TCPServer的连接处理器为HTTPServer的成员函数HandleClient。这样，每当TCPServer接受到一个新的连接时，都会调用HTTPServer的HandleClient来处理这个连接。
            server_.SetConnectionHandler([this](int client_fd)
                                         { HandleClient(client_fd); });
        }

        // 获取Router，用于注册路由
        Router &GetRouter()
        {
            return router_;
        }

        // 设置自定义请求处理器（与Router互补）
        void SetRequestHandler(RequestHandler handler)
        {
            handler_ = std::move(handler);
        }

        bool Start()
        {
            return server_.Start();
        }

        void Stop()
        {
            server_.Stop();
        }

    private:
        // handleClient的作用是处理每个新连接的客户端请求。它会从客户端套接字接收原始HTTP请求数据，解析出Request对象，然后调用用户定义的RequestHandler来生成Response对象，最后将Response发送回客户端。
        void HandleClient(int client_fd)
        {
            // 应用层接收蓄水池，用于缓存从网络读取的未处理字节流
            std::string receive_buffer;
            receive_buffer.reserve(4096);

            char buffer[4096];

            // 外层循环：在长连接生命周期内，持续处理该 Socket 上连续到来的多个请求
            while (true)
            {
                size_t header_end_pos = std::string::npos;
                size_t content_length = 0;
                bool has_body = false;

                // ================== 【步骤 1：状态机拼包】 ==================
                while (true)
                {
                    // 每次读取前，先扫描蓄水池中是否已经积累了完整的 Header
                    header_end_pos = receive_buffer.find("\r\n\r\n");

                    if (header_end_pos != std::string::npos)
                    {
                        // 状态 1 闭环：抓取 Header 切片，分析 Content-Length 边界
                        std::string header_part = receive_buffer.substr(0, header_end_pos);

                        size_t cl_pos = header_part.find("Content-Length:");
                        if (cl_pos != std::string::npos)
                        {
                            size_t value_start = cl_pos + 15; // "Content-Length:" 长度
                            size_t value_end = header_part.find("\r\n", value_start);
                            if (value_end != std::string::npos)
                            {
                                std::string cl_str = detail::Trim(header_part.substr(value_start, value_end - value_start));
                                content_length = std::stoul(cl_str);
                                has_body = (content_length > 0);
                            }
                        }

                        // 状态 2 判断：根据协议字段，检查整个请求是否接收完毕
                        if (!has_body)
                        {
                            break; // GET 等无正文请求，Header 齐了直接收工
                        }

                        size_t current_body_len = receive_buffer.size() - (header_end_pos + 4);
                        if (current_body_len >= content_length)
                        {
                            break; // POST 等有正文请求，Body 字节数也攒够了，收工
                        }
                    }

                    // 蓄水池数据不够组成一个完整包，调用底层系统调用从网络读数据
                    ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);
                    if (bytes_received > 0)
                    {
                        // 拼接到蓄水池末尾
                        receive_buffer.append(buffer, static_cast<size_t>(bytes_received));
                        continue;
                    }
                    if (bytes_received == 0)
                    {
                        // 对端发来了 FIN，这才是真正的优雅断开
                        return;
                    }
                    // bytes_received < 0，client_fd 是非阻塞socket，必须区分 errno：
                    // EAGAIN/EWOULDBLOCK 只代表"当前没数据"，不代表连接有问题（比如请求被拆成多个
                    // TCP段还没收全，或者keep-alive连接正在空闲等待下一个请求）
                    if (errno == EINTR)
                    {
                        continue; // 被信号打断，重试即可
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        // 用 poll 阻塞等待可读事件，避免忙轮询空耗CPU；同时设超时防止连接被永远挂起
                        pollfd pfd{};
                        pfd.fd = client_fd;
                        pfd.events = POLLIN;
                        int poll_ret = poll(&pfd, 1, config_.read_timeout_ms);
                        if (poll_ret > 0)
                        {
                            continue; // 数据到了，回去重新 recv
                        }
                        if (poll_ret == 0)
                        {
                            return; // 等待超时：慢速/异常连接或keep-alive空闲超时，主动断开
                        }
                        if (errno == EINTR)
                        {
                            continue; // poll 被信号打断，重试
                        }
                        return; // poll 本身出错，放弃这条连接
                    }
                    // 其他 errno：真实的系统调用错误，连接不可用
                    return;
                }

                // ================== 【步骤 2：精准裁剪边界】 ==================
                // 计算当前这个 HTTP 请求的总字节数（Header + \r\n\r\n + Body）
                size_t total_request_len = header_end_pos + 4 + (has_body ? content_length : 0);

                // 从蓄水池中精准截取出不多不少的原始请求包
                std::string raw_request = receive_buffer.substr(0, total_request_len);

                // 【灵魂抹除】：把消费掉的数据从蓄水池头部刮掉，多读的下一个请求的内容自然顶到最前端
                receive_buffer.erase(0, total_request_len);

                // ================== 【步骤 3：反序列化与路由分发】 ==================
                // 拿着不多不少的包，调用你的 Request::Parse 接口
                // req_opt 是 std::optional<Request> 类型，可能包含一个合法的 Request 对象，也可能因为解析失败而不包含任何值（std::nullopt）
                auto req_opt = Request::Parse(raw_request);

                Response res;
                if (!req_opt.has_value())
                {
                    res = Response::BadRequest("HTTP Request Parse Error");
                    SendResponse(client_fd, res);
                    return; // 协议格式都错了，直接挂断连接
                }

                const Request &req = req_opt.value();

                // 业务层接管：优先使用Router进行路由匹配
                auto route_handler = router_.Match(req);
                if (route_handler.has_value())
                {
                    res = route_handler.value()(req);
                }
                else if (handler_)
                {
                    // 如果Router没有匹配到路由，则使用自定义的请求处理器
                    res = handler_(req);
                }
                else
                {
                    // 如果既没有匹配到路由，也没有自定义处理器，则查找默认处理器
                    auto default_handler = router_.GetDefaultHandler();
                    if (default_handler.has_value())
                    {
                        res = default_handler.value()(req);
                    }
                    else
                    {
                        res = Response::NotFound();
                    }
                }

                // 将 Response 转换为标准文本流并发送
                SendResponse(client_fd, res);

                // ================== 【步骤 4：长连接决策】 ==================
                // 检查请求中是否显式要求断开
                auto conn_header = req_opt.value().GetHeader("Connection");
                if (conn_header.has_value() && detail::ToLower(conn_header.value()) == "close")
                {
                    return; // 满足客户端要求，优雅退出，外层 detach 线程会关闭 client_fd
                }
            }
        }
        void SendResponse(int client_fd, const Response &res) const
        {
            std::string raw_response = res.ToString();
            send(client_fd, raw_response.c_str(), raw_response.size(), 0);
        }

        ServerConfig config_;
        TCPServer server_;
        Router router_;
        RequestHandler handler_;
    };

} // namespace http
