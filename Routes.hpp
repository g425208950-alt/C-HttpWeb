#pragma once

#include "Router.hpp"
#include <ctime>
#include <string>

namespace http
{
    // 首页路由：GET /
    inline void RegisterHomeRoutes(Router &router)
    {
        router.Get("/", [](const Request & /*req*/)
        {
            Response res;
            res.status = 200;
            res.reason = "OK";
            res.SetHtmlBody(
                "<html>"
                "<head><title>C++ Web Server</title></head>"
                "<body>"
                "    <h1>🚀 C++ HTTP Web Server</h1>"
                "    <p>这是一个由 C++ 搭建的 HTTP 服务器，支持完整的路由和序列化功能。</p>"
                "    <h2>API 列表：</h2>"
                "    <ul>"
                "        <li>GET /api/status - 获取服务器状态</li>"
                "        <li>GET /api/hello?name=xxx - 问好接口</li>"
                "        <li>GET /api/user/:id - 获取用户信息（示例）</li>"
                "        <li>POST /api/echo - 回显请求体</li>"
                "    </ul>"
                "</body>"
                "</html>");
            return res;
        });
    }

    // 状态/问好类只读接口：GET /api/status, GET /api/hello
    inline void RegisterStatusRoutes(Router &router)
    {
        router.Get("/api/status", [](const Request & /*req*/)
        {
            Response res;
            res.status = 200;
            res.reason = "OK";
            res.SetJsonBody("{\"status\":\"running\",\"msg\":\"Server is healthy\",\"timestamp\":" +
                            std::to_string(time(nullptr)) + "}");
            return res;
        });

        router.Get("/api/hello", [](const Request &req)
        {
            // 演示如何获取查询参数
            auto name_opt = req.GetQueryParam("name");
            std::string name = name_opt.has_value() ? name_opt.value() : "Guest";

            std::string json = "{\"status\":200,\"msg\":\"Hello " + detail::JsonEscape(name) +
                              "\",\"time\":" + std::to_string(time(nullptr)) + "}";

            Response res;
            res.status = 200;
            res.reason = "OK";
            res.SetJsonBody(json);
            return res;
        });
    }

    // 回显接口：POST /api/echo
    inline void RegisterEchoRoutes(Router &router)
    {
        router.Post("/api/echo", [](const Request &req)
        {
            std::string json = "{\"status\":200,\"msg\":\"Echo received\",\"body\":\"" +
                              detail::JsonEscape(req.body) + "\"}";

            Response res;
            res.status = 200;
            res.reason = "OK";
            res.SetJsonBody(json);
            return res;
        });
    }

    // 用户相关接口：GET /api/user/:id, POST /api/user
    inline void RegisterUserRoutes(Router &router)
    {
        // 使用正则表达式匹配 /api/user/123, /api/user/456 等
        router.RegisterRegex(Method::GET, "/api/user/(\\d+)", [](const Request &req)
        {
            // 从路径中提取用户ID
            size_t pos = req.path.rfind('/');
            std::string user_id = (pos != std::string::npos) ? req.path.substr(pos + 1) : "0";

            std::string json = "{\"status\":200,\"user_id\":" + user_id +
                              ",\"name\":\"User" + user_id + "\",\"email\":\"user" + user_id + "@example.com\"}";

            Response res;
            res.status = 200;
            res.reason = "OK";
            res.SetJsonBody(json);
            return res;
        });

        router.Post("/api/user", [](const Request & /*req*/)
        {
            // 这里可以解析JSON请求体并创建用户
            std::string json = "{\"status\":201,\"msg\":\"User created successfully\",\"id\":12345}";

            Response res;
            res.status = 201;
            res.reason = "Created";
            res.SetJsonBody(json);
            return res;
        });
    }

    // 404兜底
    inline void RegisterFallbackRoutes(Router &router)
    {
        router.SetDefaultHandler([](const Request & /*req*/)
        {
            return Response::NotFound();
        });
    }

    // 一次性注册本项目所有示例路由
    inline void RegisterAppRoutes(Router &router)
    {
        RegisterHomeRoutes(router);
        RegisterStatusRoutes(router);
        RegisterEchoRoutes(router);
        RegisterUserRoutes(router);
        RegisterFallbackRoutes(router);
    }

} // namespace http
