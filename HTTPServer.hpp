#pragma once

#include "TCPServer.hpp"
#include "HTTP.hpp"
#include "Router.hpp"
#include "ServerConfig.hpp"
#include <string>
#include <optional>
#include <limits>

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
                {
                    // 头没收齐就攒满了头部上限：恶意超长头，拒绝并关闭（防内存打爆）
                    if (conn.recv_buf.size() > config_.max_header_bytes)
                    {
                        QueueResponseAndClose(conn, Response::BadRequest("Header too large"));
                        return;
                    }
                    return; // 头还没收齐，等下次数据（此时由传输层的请求总时限兜底）
                }

                // 头已完整：头部本身也不许超限（防一次性 burst 大头绕过上面的渐进检查）
                if (header_end_pos > config_.max_header_bytes)
                {
                    QueueResponseAndClose(conn, Response::BadRequest("Header too large"));
                    return;
                }

                // ---- 步骤2：安全解析 Content-Length，判断 body 是否收齐 ----
                std::string header_part = conn.recv_buf.substr(0, header_end_pos);

                size_t content_length = 0;
                bool has_body = false;
                size_t cl_pos = header_part.find("Content-Length:");
                if (cl_pos != std::string::npos)
                {
                    size_t value_start = cl_pos + 15; // "Content-Length:" 长度
                    size_t value_end = header_part.find("\r\n", value_start);
                    if (value_end == std::string::npos)
                        value_end = header_part.size(); // CL 恰好是最后一行头（后面直接跟空行）

                    std::string cl_str = detail::Trim(header_part.substr(value_start, value_end - value_start));
                    auto cl_opt = ParseNonNegative(cl_str);
                    if (!cl_opt.has_value())
                    {
                        // 非数字/负数/溢出：恶意或畸形请求。原实现裸用 std::stoul 会抛异常，
                        // 打穿事件循环线程导致整个进程 std::terminate，这里改为安全拒绝
                        QueueResponseAndClose(conn, Response::BadRequest("Invalid Content-Length"));
                        return;
                    }
                    content_length = cl_opt.value();
                    has_body = (content_length > 0);
                }

                // body 上限：声明值超限直接 413，不等真收满（收满本身就会被内存上限拦）
                if (has_body && content_length > config_.max_body_bytes)
                {
                    QueueResponseAndClose(conn, Response::PayloadTooLarge());
                    return;
                }

                size_t total_request_len = header_end_pos + 4 + (has_body ? content_length : 0);
                if (conn.recv_buf.size() < total_request_len)
                    return; // body 还没收齐，等下次数据（总量已被校验过的 CL 封顶，缓冲有界）

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

        // 把响应排进发送缓冲，并标记写完即关（用于各类协议层拒绝场景）
        void QueueResponseAndClose(TCPServer::Connection &conn, Response res)
        {
            conn.send_buf.append(res.ToString());
            conn.closing = true;
            conn.keep_alive = false;
        }

        // 安全解析非负十进制整数：空串/含非数字/溢出均返回 nullopt（替代会抛异常的 std::stoul）
        static std::optional<size_t> ParseNonNegative(const std::string &s)
        {
            if (s.empty())
                return std::nullopt;
            size_t value = 0;
            const size_t kMax = std::numeric_limits<size_t>::max();
            for (char c : s)
            {
                if (c < '0' || c > '9')
                    return std::nullopt;
                size_t digit = static_cast<size_t>(c - '0');
                if (value > (kMax - digit) / 10)
                    return std::nullopt; // 再乘 10 会溢出
                value = value * 10 + digit;
            }
            return value;
        }

        ServerConfig config_;
        TCPServer server_;
        Router router_;
        RequestHandler handler_;
    };

} // namespace http
