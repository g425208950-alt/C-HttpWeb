#pragma once

#include "HTTP.hpp"
#include <unordered_map>
#include <vector>
#include <regex>
#include <functional>

namespace http
{
    // 路由处理器类型：接收Request，返回Response
    using RouteHandler = std::function<Response(const Request &)>;

    // 路由表项：包含方法、路径模式和处理器
    struct Route
    {
        Method method;
        std::string path_pattern; // 支持精确匹配和正则表达式匹配
        RouteHandler handler;
        bool is_regex = false; // 是否使用正则表达式
    };

    class Router
    {
    public:
        Router() = default;

        // 注册一个精确路由
        void Register(Method method, const std::string &path, RouteHandler handler)
        {
            Route r;
            r.method = method;
            r.path_pattern = path;
            r.handler = handler;
            r.is_regex = false;
            routes_.push_back(r);
        }

        // 注册一个正则表达式路由
        void RegisterRegex(Method method, const std::string &pattern, RouteHandler handler)
        {
            Route r;
            r.method = method;
            r.path_pattern = pattern;
            r.handler = handler;
            r.is_regex = true;
            routes_.push_back(r);
        }

        // 便捷方法：注册GET路由
        void Get(const std::string &path, RouteHandler handler)
        {
            Register(Method::GET, path, handler);
        }

        // 便捷方法：注册POST路由
        void Post(const std::string &path, RouteHandler handler)
        {
            Register(Method::POST, path, handler);
        }

        // 便捷方法：注册PUT路由
        void Put(const std::string &path, RouteHandler handler)
        {
            Register(Method::PUT, path, handler);
        }

        // 便捷方法：注册DELETE路由
        void Delete(const std::string &path, RouteHandler handler)
        {
            Register(Method::DELETE, path, handler);
        }

        // 匹配请求并返回对应的处理器
        // 返回值：std::optional<RouteHandler>，如果匹配成功则返回处理器，否则返回std::nullopt
        std::optional<RouteHandler> Match(const Request &req) const
        {
            for (const auto &route : routes_)
            {
                // 首先检查HTTP方法是否匹配
                if (route.method != req.method)
                    continue;

                // 检查路径是否匹配
                bool path_matches = false;
                if (route.is_regex)
                {
                    // 正则表达式匹配
                    try
                    {
                        std::regex pattern(route.path_pattern);
                        path_matches = std::regex_match(req.path, pattern);
                    }
                    catch (const std::exception &)
                    {
                        // 正则表达式有效性检查失败，跳过此路由
                        continue;
                    }
                }
                else
                {
                    // 精确匹配
                    path_matches = (route.path_pattern == req.path);
                }

                if (path_matches)
                    return route.handler;
            }

            return std::nullopt;
        }

        // 设置默认处理器（当没有匹配的路由时调用）
        void SetDefaultHandler(RouteHandler handler)
        {
            default_handler_ = handler;
        }

        // 获取默认处理器
        std::optional<RouteHandler> GetDefaultHandler() const
        {
            if (default_handler_)
                return default_handler_;
            return std::nullopt;
        }

        // 清空所有路由
        void Clear()
        {
            routes_.clear();
            default_handler_ = nullptr;
        }

        // 获取已注册的路由数量
        size_t RouteCount() const
        {
            return routes_.size();
        }

    private:
        std::vector<Route> routes_; // 存储所有注册的路由
        RouteHandler default_handler_ = nullptr; // 默认处理器
    };

} // namespace http
