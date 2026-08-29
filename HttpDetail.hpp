#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_map>

namespace http
{
    // 纯工具类函数集合，不依赖 Request/Response 等业务类型。
    namespace detail
    {
        // Trim 字符串首尾空白字符（返回子串视图，不拷贝）
        inline std::string_view TrimView(std::string_view s)
        {
            const std::string whitespaces = " \n\t\f\v\r";
            size_t start = s.find_first_not_of(whitespaces);
            if (start == std::string::npos)
            {
                return "";
            }
            size_t end = s.find_last_not_of(whitespaces);
            s.remove_prefix(start);
            s.remove_suffix(s.size() - (end + 1));

            return s;
        }

        // 单个十六进制字符转十进制（0-15），非法字符返回 nullopt
        inline std::optional<int> HexToDec(const char c)
        {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' &&
                 c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
            {
                return c - 'A' + 10;
            }
            return std::nullopt;
        }

        // Trim 字符串首尾空白字符（返回拷贝的新字符串）
        inline std::string Trim(const std::string& s)
        {
            const std::string whitespaces = " \n\t\f\v\r";
            size_t start = s.find_first_not_of(whitespaces);
            if (start == std::string::npos)
            {
                return "";
            }
            size_t end = s.find_last_not_of(whitespaces);
            std::string res = s.substr(start, end - start + 1);

            return res;
        }

        // 转小写（返回拷贝的新字符串）
        inline std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned int c)
                           { return std::tolower(c); });
            return s;
        }

        // 在 header map 中按大小写不敏感的方式查找一个键（Request/Response 共用）
        inline std::optional<std::string> FindHeaderCI(const std::unordered_map<std::string, std::string> &headers, const std::string &key)
        {
            auto it = headers.find(key);
            if (it != headers.end())
            {
                return it->second;
            }
            const std::string low_key = ToLower(key);
            for (const auto &p : headers)
            {
                if (ToLower(p.first) == low_key)
                {
                    return p.second;
                }
            }
            return std::nullopt;
        }

        // URL解码：将 %XX 与 '+' 转换回原始字符
        inline std::string UrlDecode(const std::string &encoded, bool quest_or_not)
        {
            std::string decoded;
            for (size_t i = 0; i < encoded.size(); i++)
            {
                if (encoded[i] == '%' && i + 2 < encoded.size())
                {

                    auto val_front = detail::HexToDec(encoded[i + 1]);
                    auto val_behind = detail::HexToDec(encoded[i + 2]);

                    if (val_front == std::nullopt || val_behind == std::nullopt)
                    {
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

        // URL编码：将特殊字符转换为 %XX
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

                        escaped += R"haha(\u)haha" + std::string(buffer);
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
} // namespace http
