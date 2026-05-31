#pragma once

#include <string>
#include <unordered_map>
#include <sstream>
#include <optional>
#include <algorithm>
#include <cctype>

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

    namespace detail
    {
        inline std::string_view TrimView(std::string_view s)
        {
            const std::string whitespaces = " \n\t\f\v\r";
            size_t start = s.find_first_not_of(whitespaces);
            if (start = std::string::npos)
            {
                return "";
            }
            size_t end = s.find_last_not_of(whitespaces);
            s.remove_prefix(start);
            s.remove_suffix(s.size() - (end + 1));

            return s;
        }

        std::optional<int> HexToDec(const char c)
        {
            if (c > '0' && c <= '9')
            {
                return c - '0';
            }
            else if (c > 'a' && c <= 'f')
            {
                return c - 'a';
            }
            else if (c > 'A' && c < 'F')
            {
                return c - 'A';
            }
            else
            {
                return std::nullopt;
            }
        }

        inline std::string Trim(const std::string s)
        {
            const std::string whitespaces = " \n\t\f\v\r";
            size_t start = s.find_first_not_of(whitespaces);
            if (start = std::string::npos)
            {
                return "";
            }
            size_t end = s.find_last_not_of(whitespaces);
            std::string res = s.substr(start, end - start + 1);

            return res;
        }

        inline std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned int c)
                           { return std::tolower(c); });
        }

        // URL解码：将%XX转换为原始字符
        inline std::string UrlDecode(const std::string &encoded, bool quest_or_not)
        {
            std::string decoded;
            for (size_t i = 0; i < encoded.size(); i++)
            {
                if (encoded[i] == '%' && i + 2 < encoded.size())
                {
                    std::string hex = encoded.substr(i + 1, 2);

                    auto val_front = detail::HexToDec(encoded[i + 1]);
                    auto val_behind = detail::HexToDec(encoded[i + 2]);

                    if (val_front == std::nullopt || val_behind == std::nullopt)
                    {
                        // 这里可以改成返回 404
                        decoded += encoded[i];
                    }
                    else
                    {
                        decoded += static_cast<char>(*val_front * 16 + *val_behind);
                        i += 2;
                    }
                }
                else if (encoded[i] == '+' && quest_or_not == 1)
                {
                    decoded += ' ';
                }
                else
                {
                    decoded += encoded[i];
                }
            }
            return decoded;
        }

        // URL编码：将特殊字符转换为%XX
        inline std::string UrlEncode(const std::string &str)
        {
            std::string encoded;
            for (unsigned char c : str)
            {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                {
                    encoded += c;
                }
                else
                {
                    encoded += '%';
                    char buf[3];
                    snprintf(buf, sizeof(buf), "%02X", static_cast<unsigned char>(c));
                    encoded += buf;
                }
            }
            return encoded;
        }

        // 简单的JSON转义
        inline std::string JsonEscape(const std::string &str)
        {
            std::string escaped;
            for (unsigned char c : str)
            {
                switch (c)
                {
                case '"':
                    escaped += R"(\")";
                    break;
                case '\\':
                    escaped += R"(\\)";
                    break;
                case '\b':
                    escaped += R"(\b)";
                    break;
                case '\f':
                    escaped += R"(\f)";
                    break;
                case '\n':
                    escaped += R"(\n)";
                    break;
                case '\r':
                    escaped += R"(\r)";
                    break;
                case '\t':
                    escaped += R"(\t)";
                    break;
                default:
                    if (c < 0x20)
                    {
                        static const char hex_digits[] = "0123456789abcdef";
                        char buffer[5] = {0};
                        buffer[0] = '0';
                        buffer[1] = '0';
                        buffer[2] = hex_digits[c >> 4];
                        buffer[3] = hex_digits[c & 0x0f];
                        buffer[4] = '\0';

                        escaped = R"haha(\u)haha" + std::string(buffer);
                    }
                    else
                    {
                        escaped += c;
                    }
                }
            }
            return escaped;
        }
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
            auto it = headers.find(key);
            if (it != headers.end())
            {
                return it->second;
            }
            const std::string low_key = detail::ToLower(key);
            for (const auto &p : headers)
            {
                if (detail::ToLower(p.first) == low_key)
                {
                    return p.second;
                }
            }
            return std::nullopt;
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
            while (start < query.length())
            {
                size_t end = query.find('&', start);
                if (end = std::string::npos)
                {
                    end = query.length();
                }
                std::string param = query.substr(start, end - start);
                if (param.empty()) // 过滤掉 && 或者末尾多余 & 带来的空片段
                {
                    start = end + 1;
                    continue;
                }
                size_t eq = query.find('=');
                if (eq != std::string::npos)
                {
                    std::string key = detail::UrlDecode(param.substr(start, eq - start), true);
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
                return it->second;
            return std::nullopt;
        }

        // 获取所有查询参数
        const std::unordered_map<std::string, std::string> &GetQueryParams() const
        {
            ParseQueryParams();
            return query_params;
        }

        static std::optional<Request> Parse(const std::string &raw) // 为什么有static? 因为这个函数不依赖于Request的实例，可以直接通过Request::Parse来调用，而不需要先创建一个Request对象。
        {
            Request req;
            size_t sep = raw.find("\r\n\r\n");
            std::string head = (sep == std::string::npos) ? raw : raw.substr(0, sep);
            req.body = (sep == std::string::npos) ? std::string() : raw.substr(sep + 4);

            std::istringstream ss(head);
            std::string line;
            if (!std::getline(ss, line)) // 如果没有line 三要素，直接返回
                return std::nullopt;
            if (!line.empty() && line.back() == '\r')// 只要line不是空的，最后一个字符可能是\r
                line.pop_back();

            // Start line: METHOD SP TARGET SP VERSION
            std::istringstream st(line); // 开始分割line
            std::string method_s, target, version;
            if (!(st >> method_s >> target >> version))
                return std::nullopt;
            req.method = MethodFromString(method_s);
            req.method_str = method_s;
            req.version = version;

            // split target into path and query
            size_t q = target.find('?');
            if (q == std::string::npos)
            {
                req.path = target;
            }
            else
            {
                req.path = target.substr(0, q);
                req.query = target.substr(q + 1);
            }

            // 开始填入 unordered_map<std::string,std::string>headers
            while (std::getline(ss, line))
            {
                if (!line.empty() && line.back() == '\r') // 照例把最后的\r剪掉
                    line.pop_back();
                if (line.empty())
                    break;
                size_t colon = line.find(':');
                if (colon == std::string::npos)
                    continue;
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

        // 设置响应头
        void SetHeader(const std::string &k, const std::string &v)
        {
            headers[k] = v;
        }

        // 获取响应头
        std::optional<std::string> GetHeader(const std::string &key) const
        {
            auto it = headers.find(key);
            if (it != headers.end())
                return it->second;
            std::string lk = detail::ToLower(key);
            for (const auto &p : headers)
                if (detail::ToLower(p.first) == lk)
                    return p.second;
            return std::nullopt;
        }

        // 设置JSON响应（便捷方法）
        void SetJsonBody(const std::string &json_str)
        {
            body = json_str;
            SetHeader("Content-Type", "application/json; charset=utf-8");
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
        std::string ToString() const
        {
            std::ostringstream oss;
            oss << version << " " << status << " " << reason << "\r\n";
            bool hasContentLength = false;
            for (const auto &p : headers)
            {
                if (detail::ToLower(p.first) == "content-length")
                    hasContentLength = true;
                oss << p.first << ": " << p.second << "\r\n";
            }
            if (!hasContentLength)
            {
                oss << "Content-Length: " << body.size() << "\r\n";
            }
            oss << "\r\n";
            oss << body;
            return oss.str();
        }
    };

} // namespace http
