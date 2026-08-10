#include "HTTPServer.hpp"
#include <iostream>
#include <ctime>
#include <thread>
#include <chrono>

int main()
{
    // 消除行缓冲
    std::cout.setf(std::ios::unitbuf);

    const int PORT = 8080;

    // 1. 初始化HTTP服务器实例，监听8080端口
    http::HTTPServer server(PORT);

    // 2. 【核心特性】通过Router注册各种路由处理程序
    http::Router &router = server.GetRouter();

    // 【首页路由】GET /
    router.Get("/", [](const http::Request & /*req*/)
    {
        http::Response res;
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

    // 【API：状态查询】GET /api/status
    router.Get("/api/status", [](const http::Request & /*req*/)
    {
        http::Response res;
        res.status = 200;
        res.reason = "OK";
        res.SetJsonBody("{\"status\":\"running\",\"msg\":\"Server is healthy\",\"timestamp\":" + 
                        std::to_string(time(nullptr)) + "}");
        return res;
    });

    // 【API：问好接口】GET /api/hello?name=xxx
    router.Get("/api/hello", [](const http::Request &req)
    {
        // 演示如何获取查询参数
        auto name_opt = req.GetQueryParam("name");
        std::string name = name_opt.has_value() ? name_opt.value() : "Guest";

        std::string json = "{\"status\":200,\"msg\":\"Hello " + http::detail::JsonEscape(name) + 
                          "\",\"time\":" + std::to_string(time(nullptr)) + "}";

        http::Response res;
        res.status = 200;
        res.reason = "OK";
        res.SetJsonBody(json);
        return res;
    });

    // 【API：POST 回显】POST /api/echo
    router.Post("/api/echo", [](const http::Request &req)
    {
        // 获取请求体中的内容
        std::string json = "{\"status\":200,\"msg\":\"Echo received\",\"body\":\"" + 
                          http::detail::JsonEscape(req.body) + "\"}";

        http::Response res;
        res.status = 200;
        res.reason = "OK";
        res.SetJsonBody(json);
        return res;
    });

    // 【API：用户信息】GET /api/user/ (使用正则表达式匹配 /api/user/123, /api/user/456 等)
    router.RegisterRegex(http::Method::GET, "/api/user/(\\d+)", [](const http::Request &req)
    {
        // 从路径中提取用户ID
        size_t pos = req.path.rfind('/');
        std::string user_id = (pos != std::string::npos) ? req.path.substr(pos + 1) : "0";

        std::string json = "{\"status\":200,\"user_id\":" + user_id + 
                          ",\"name\":\"User" + user_id + "\",\"email\":\"user" + user_id + "@example.com\"}";

        http::Response res;
        res.status = 200;
        res.reason = "OK";
        res.SetJsonBody(json);
        return res;
    });

    // 【API：POST 创建用户】POST /api/user
    router.Post("/api/user", [](const http::Request & /*req*/)
    {
        // 这里可以解析JSON请求体并创建用户
        std::string json = "{\"status\":201,\"msg\":\"User created successfully\",\"id\":12345}";

        http::Response res;
        res.status = 201;
        res.reason = "Created";
        res.SetJsonBody(json);
        return res;
    });

    // 【默认处理器】处理404错误
    router.SetDefaultHandler([](const http::Request & /*req*/)
    {
        return http::Response::NotFound();
    });

    std::cout << "========================================\n";
    std::cout << "HTTP 服务器启动\n";
    std::cout << "监听地址: http://127.0.0.1:" << PORT << "\n";
    std::cout << "已注册的路由数: " << router.RouteCount() << "\n";
    std::cout << "========================================\n";
    std::cout << "请打开浏览器访问: http://127.0.0.1:" << PORT << "\n";
    std::cout << "或使用 curl 测试 API:\n";
    std::cout << "  curl http://127.0.0.1:" << PORT << "/api/status\n";
    std::cout << "  curl http://127.0.0.1:" << PORT << "/api/hello?name=Alice\n";
    std::cout << "  curl -X POST http://127.0.0.1:" << PORT << "/api/echo -d 'test data'\n";
    std::cout << "========================================\n\n";

    // 3. 启动服务器（内部会开启 AcceptLoop 后台子线程）
    if (!server.Start())
    {
        std::cerr << "❌ 服务器启动失败！可能是端口被占用了。\n";
        return -1;
    }

    std::cout << "✅ 服务器已启动，按 Ctrl+C 停止\n\n";

    // 4. 防止主线程立刻退出
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}