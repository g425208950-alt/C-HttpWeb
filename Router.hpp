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
        bool is_regex = false;   // 是否使用正则表达式
        std::regex compiled_regex; // is_regex为true时，注册阶段就编译好，避免每次请求重新编译
    };

    class Router
    {
    public:
        Router() = default;

        // 注册一个精确路由：用 "METHOD path" 做key存进unordered_map，Match时O(1)查找
        void Register(Method method, const std::string &path, RouteHandler handler)
        {
            Route r;
            r.method = method;
            r.path_pattern = path;
            r.handler = handler;
            r.is_regex = false;
            exact_routes_[ExactKey(method, path)] = std::move(r);
        }

        // 注册一个正则表达式路由：编译在注册阶段就完成一次，Match时直接复用，不再重复编译。
        // 正则语法错误会在这里直接抛出std::regex_error，属于开发期就该发现的编程错误。
        void RegisterRegex(Method method, const std::string &pattern, RouteHandler handler)
        {
            Route r;
            r.method = method;
            r.path_pattern = pattern;
            r.handler = handler;
            r.is_regex = true;
            r.compiled_regex = std::regex(pattern);
            regex_routes_.push_back(std::move(r));
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
            // 精确匹配：O(1) 哈希查找，绝大多数请求走这条路
            auto it = exact_routes_.find(ExactKey(req.method, req.path));
            if (it != exact_routes_.end())
                return it->second.handler;

            // 正则路由数量通常很少，按注册顺序线性尝试，正则已在注册时编译好
            for (const auto &route : regex_routes_)
            {
                if (route.method != req.method)
                    continue;
                if (std::regex_match(req.path, route.compiled_regex))
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
            exact_routes_.clear();
            regex_routes_.clear();
            default_handler_ = nullptr;
        }

        // 获取已注册的路由数量
        size_t RouteCount() const
        {
            return exact_routes_.size() + regex_routes_.size();
        }

    private:
        // 精确路由的查找key："METHOD path"，同一方法+路径重复注册时后一次会覆盖前一次
        static std::string ExactKey(Method method, const std::string &path)
        {
            return MethodToString(method) + " " + path;
        }

        std::unordered_map<std::string, Route> exact_routes_; // 精确匹配路由：O(1)查找
        std::vector<Route> regex_routes_;                       // 正则匹配路由：按注册顺序线性尝试
        RouteHandler default_handler_ = nullptr;                // 默认处理器
    };

} // namespace http
