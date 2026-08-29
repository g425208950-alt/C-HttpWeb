#pragma once

#include "TCPServer.hpp"
#include "HTTP.hpp"
#include "Router.hpp"
#include "ServerConfig.hpp"
#include <string>

namespace http
{
    class HTTPServer
    {
    public:
        using RequestHandler = std::function<Response(const Request &)>;

        explicit HTTPServer(const ServerConfig &config)
            : config_(config), server_(config)
        {
            // 事件驱动：每条连接收到数据后，循环线程把 Connection 交给本回调，
            // 在此从 recv_buf 解析完整 HTTP 请求、跑路由、把响应序列化塞进 send_buf。
            server_.SetOnReadable([this](TCPServer::Connection &conn)
                                  { OnReadable(conn); });
        }

        Router &GetRouter() { return router_; }

        void SetRequestHandler(RequestHandler handler)
        {
            handler_ = std::move(handler);
        }

        bool Start() { return server_.Start(); }
        void Stop() { server_.Stop(); }

    private:
        // 业务层入口：从 recv_buf 中循环解出完整请求并填充 send_buf（支持 pipelining）。
        // 数据不够组成一个完整包时直接返回，等下一次 EPOLLIN 带来更多数据。
        // I/O 本身（非阻塞 read/write、写半包、ET drain）由 TCPServer 负责，这里只处理协议。
        void OnReadable(TCPServer::Connection &conn)
        {
            while (!conn.closing && !conn.recv_buf.empty())
            {
                // ---- 步骤1：在蓄水池里找请求头结束标志 ----
                size_t header_end_pos = conn.recv_buf.find("\r\n\r\n");
                if (header_end_pos == std::string::npos)
                    return; // 头还没收齐，等下次数据

                // ---- 步骤2：从头部解析 Content-Length，判断 body 是否收齐 ----
                std::string header_part = conn.recv_buf.substr(0, header_end_pos);

                size_t content_length = 0;
                bool has_body = false;
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

                size_t total_request_len = header_end_pos + 4 + (has_body ? content_length : 0);
                if (conn.recv_buf.size() < total_request_len)
                    return; // body 还没收齐，等下次数据

                // ---- 步骤3：精准裁剪出一条完整请求，从蓄水池头部抹掉 ----
                std::string raw_request = conn.recv_buf.substr(0, total_request_len);
                conn.recv_buf.erase(0, total_request_len);

                // ---- 步骤4：反序列化 + 路由分发，生成 Response ----
                auto req_opt = Request::Parse(raw_request);

                Response res;
                bool should_close = false;

                if (!req_opt.has_value())
                {
                    res = Response::BadRequest("HTTP Request Parse Error");
                    should_close = true; // 协议格式错误，发完错误响应即挂断
                }
                else
                {
                    const Request &req = req_opt.value();

                    auto route_handler = router_.Match(req);
                    if (route_handler.has_value())
                        res = route_handler.value()(req);
                    else if (handler_)
                        res = handler_(req);
                    else
                    {
                        auto default_handler = router_.GetDefaultHandler();
                        res = default_handler.has_value() ? default_handler.value()(req)
                                                          : Response::NotFound();
                    }

                    // keep-alive 决策：客户端显式要求 close 则写完即断
                    auto conn_header = req.GetHeader("Connection");
                    if (conn_header.has_value() && detail::ToLower(conn_header.value()) == "close")
                        should_close = true;
                }

                // ---- 步骤5：序列化响应，追加进 send_buf（循环线程随后负责写出）----
                conn.send_buf.append(res.ToString());

                if (should_close)
                {
                    conn.closing = true;
                    conn.keep_alive = false;
                    break; // 不再继续解后续 pipelining 请求
                }
            }
        }

        ServerConfig config_;
        TCPServer server_;
        Router router_;
        RequestHandler handler_;
    };

} // namespace http
