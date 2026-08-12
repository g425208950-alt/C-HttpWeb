#pragma once

#include <string>
#include <unordered_map>
#include <sstream>
#include <optional>
#include <utility>

#include "HttpDetail.hpp"

namespace http
{

    // Method represents the HTTP method of a request (e.g., GET, POST, etc.)
    enum class Method
    {
        GET,
        POST,
        PUT,
        DELETE,
        HEAD,
        OPTIONS,
        PATCH,
        UNKNOWN
    };

    // MethodToString converts a Method enum value to its corresponding string representation (e.g., Method::GET -> "GET").
    // 什么时候需要把枚举转换成字符串？当你需要在日志、调试输出、HTTP响应头等地方以文本形式表示HTTP方法时，就需要这个函数。
    inline std::string MethodToString(Method m)
    {
        switch (m)
        {
        case Method::GET:
            return "GET";
        case Method::POST:
            return "POST";
        case Method::PUT:
            return "PUT";
        case Method::DELETE:
            return "DELETE";
        case Method::HEAD:
            return "HEAD";
        case Method::OPTIONS:
            return "OPTIONS";
        case Method::PATCH:
            return "PATCH";
        default:
            return "UNKNOWN";
        }
    }

    inline Method MethodFromString(const std::string &s)
    {
        if (s == "GET")
            return Method::GET;
        if (s == "POST")
            return Method::POST;
        if (s == "PUT")
            return Method::PUT;
        if (s == "DELETE")
            return Method::DELETE;
        if (s == "HEAD")
            return Method::HEAD;
        if (s == "OPTIONS")
            return Method::OPTIONS;
        if (s == "PATCH")
            return Method::PATCH;
        return Method::UNKNOWN;
    }

    struct Request
    {
        Method method = Method::UNKNOWN;
        std::string method_str;
        std::string path;  // path without query
        std::string query; // raw query string (after '?')
        std::string version = "HTTP/1.1";
        std::unordered_map<std::string, std::string> headers;
        std::string body;
        mutable std::unordered_map<std::string, std::string> query_params; // 缓存的查询参数
        mutable bool query_params_parsed_ = false;                         // 查询参数是否已解析

        // 获取HTTP请求头，支持大小写不敏感的查找
        std::optional<std::string> GetHeader(const std::string &key) const
        {
            return detail::FindHeaderCI(headers, key);
        }

        // 解析查询参数（URL中?后的部分）
        void ParseQueryParams() const
        {
            if (query_params_parsed_)
                return;
            query_params.clear();
            if (query.empty())
            {
                query_params_parsed_ = 1;
                return;
            }
            size_t start = 0;
            while (start < query.length()) // 检测start即可
            {
                size_t end = query.find('&', start);
                if (end == std::string::npos)
                {
                    end = query.length();
                }
                std::string param = query.substr(start, end - start);
                if (param.empty()) // 过滤掉 && 或者末尾多余 & 带来的空片段
                {
                    start = end + 1;
                    continue;
                }
                size_t eq = param.find('=');
                if (eq != std::string::npos)
                {
                    std::string key = detail::UrlDecode(param.substr(0, eq), true);
                    std::string val = detail::UrlDecode(param.substr(eq + 1), true);
                    query_params[key] = val;
                }
                else
                {
                    std::string key = detail::UrlDecode(param, 1);
                    query_params[key] = "";
                }
                start = end + 1;
            }
            query_params_parsed_ = true;
        }

        // 获取查询参数
        std::optional<std::string> GetQueryParam(const std::string &key) const
        {
            ParseQueryParams();
            auto it = query_params.find(key);
            if (it != query_params.end())
            {
                return it->second;
            }
            return std::nullopt;
        }

        // 获取所有查询参数
        const std::unordered_map<std::string, std::string> &GetQueryParams() const
        {
            ParseQueryParams();
            return query_params;
        }

        static std::optional<Request> Parse(const std::string &raw)
        {
            size_t sep = raw.find("\r\n\r\n");
            // 如果连请求头结束标志都找不到，说明请求不完整
            if (sep == std::string::npos)
            {
                return std::nullopt;
            }

            Request req;
            std::string head = raw.substr(0, sep);
            req.body = raw.substr(sep + 4);

            if (head.empty())
                return std::nullopt;

            // 1. 把整个 head 放入流中
            std::istringstream ss(head);
            std::string line;

            // 2. 读取第一行（请求行）
            if (!std::getline(ss, line))
            {
                return std::nullopt;
            }

            // 清理第一行末尾可能存在的 \r
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            // 3. 解析第一行的三个字段
            std::istringstream line_ss(line);
            std::string method_s, target, version;
            if (!(line_ss >> method_s >> target >> version))
            {
                return std::nullopt;
            }

            req.method = MethodFromString(method_s);
            req.method_str = method_s;
            req.version = version;

            // 4. 解析 URL 中的 Path 和 Query
            size_t q = target.find('?');
            if (std::string::npos == q)
            {
                req.path = target;
            }
            else
            {
                req.path = target.substr(0, q);
                req.query = target.substr(q + 1);
            }

            // 5. 循环读取接下来的每一行（Headers）
            while (std::getline(ss, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }

                // 这里的 head 已经去掉了 \r\n\r\n，所以如果遇到空行，说明解析异常或结束
                if (line.empty())
                    continue;

                size_t colon = line.find(':');
                if (colon == std::string::npos)
                    continue; // 畸形行，跳过

                std::string k = detail::Trim(line.substr(0, colon));
                std::string v = detail::Trim(line.substr(colon + 1));
                req.headers[std::move(k)] = std::move(v);
            }

            return req;
        }
    };
    struct Response
    {
        std::string version = "HTTP/1.1";
        int status = 200;
        std::string reason = "OK";
        std::unordered_map<std::string, std::string> headers;
        std::string body;

        void SetHeader(const std::string &k,const std::string& v)
        {
            headers[k] = v;
        }

        // 获取响应头（大小写不敏感查找）
        std::optional<std::string> GetHeader(const std::string &key) const
        {
            return detail::FindHeaderCI(headers, key);
        }

        // 设置JSON响应（便捷方法）
        void SetJsonBody(const std::string& json_str)
        {
            body = json_str;
            SetHeader("Content-Type","application/json; charset=utf-8");
        }

        // 设置HTML响应（便捷方法）
        void SetHtmlBody(const std::string &html_str)
        {
            body = html_str;
            SetHeader("Content-Type", "text/html; charset=utf-8");
        }

        // 设置纯文本响应（便捷方法）
        void SetTextBody(const std::string &text_str)
        {
            body = text_str;
            SetHeader("Content-Type", "text/plain; charset=utf-8");
        }

        // 生成简单的JSON响应对象 {"status": status, "msg": msg}
        static Response JsonResponse(int status, const std::string &msg)
        {
            Response res;
            res.status = status;
            res.reason = (status == 200) ? "OK" : "Error";
            std::string json = "{\"status\":" + std::to_string(status) +
                               ",\"msg\":\"" + detail::JsonEscape(msg) + "\"}";
            res.SetJsonBody(json);
            return res;
        }

        // 生成404响应
        static Response NotFound()
        {
            Response res;
            res.status = 404;
            res.reason = "Not Found";
            res.SetHtmlBody("<h1>404 Not Found</h1><p>The requested resource was not found.</p>");
            return res;
        }

        // 生成400响应
        static Response BadRequest(const std::string &msg = "Bad Request")
        {
            Response res;
            res.status = 400;
            res.reason = "Bad Request";
            res.SetJsonBody("{\"status\":400,\"msg\":\"" + detail::JsonEscape(msg) + "\"}");
            return res;
        }

        // 生成500响应
        static Response InternalError(const std::string &msg = "Internal Server Error")
        {
            Response res;
            res.status = 500;
            res.reason = "Internal Server Error";
            res.SetJsonBody("{\"status\":500,\"msg\":\"" + detail::JsonEscape(msg) + "\"}");
            return res;
        }

        // 将Response序列化为HTTP文本格式
        std::string ToString() const{
            std::ostringstream oss;
            oss << version << " " << status << " " << reason << "\r\n";
            bool hasContentLength = false;
            for(const auto &p : headers)
            {
                if(detail::ToLower(p.first) == "content-length")
                    hasContentLength = true;
                oss << p.first << ": " << p.second << "\r\n";
            }
            if(!hasContentLength)
            {
                oss << "Content-Length: " << body.size() << "\r\n";
            }
            oss << "\r\n";
            oss << body;
            return oss.str();
        }
    };

} // namespace http
