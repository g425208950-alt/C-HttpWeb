#pragma once

#include <string>
#include <unordered_map>
#include <sstream>
#include <optional>
#include <algorithm>
#include <cctype>

namespace http
{

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
        inline std::string Trim(const std::string &s)
        {
            size_t a = 0;
            while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
                ++a;
            size_t b = s.size();
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
                --b;
            return s.substr(a, b - a);
        }
        inline std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                           { return std::tolower(c); });
            return s;
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

        std::optional<std::string> GetHeader(const std::string &key) const
        {
            auto it = headers.find(key);
            if (it != headers.end())
                return it->second;
            // try lowercase key
            std::string lk = detail::ToLower(key);
            for (const auto &p : headers)
                if (detail::ToLower(p.first) == lk)
                    return p.second;
            return std::nullopt;
        }

        static std::optional<Request> Parse(const std::string &raw)
        {
            Request req;    
            size_t sep = raw.find("\r\n\r\n");
            std::string head = (sep == std::string::npos) ? raw : raw.substr(0, sep);
            req.body = (sep == std::string::npos) ? std::string() : raw.substr(sep + 4);

            std::istringstream ss(head);
            std::string line;
            if (!std::getline(ss, line))
                return std::nullopt;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            // Start line: METHOD SP TARGET SP VERSION
            std::istringstream st(line);
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

            // headers
            while (std::getline(ss, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.empty())
                    break;
                size_t colon = line.find(':');
                if (colon == std::string::npos)
                    continue;
                std::string k = detail::Trim(line.substr(0, colon));
                std::string v = detail::Trim(line.substr(colon + 1));
                req.headers.emplace(k, v);
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

        void SetHeader(const std::string &k, const std::string &v) { headers[k] = v; }

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
