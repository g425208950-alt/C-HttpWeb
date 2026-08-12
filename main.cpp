#include "HTTPServer.hpp"
#include "Routes.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main()
{
    // 消除行缓冲
    std::cout.setf(std::ios::unitbuf);

    ServerConfig config;
    config.port = 8080;

    // 1. 初始化HTTP服务器实例
    http::HTTPServer server(config);

    // 2. 【核心特性】通过Router注册各种路由处理程序（具体路由定义见 Routes.hpp）
    http::RegisterAppRoutes(server.GetRouter());

    std::cout << "========================================\n";
    std::cout << "HTTP 服务器启动\n";
    std::cout << "监听地址: http://127.0.0.1:" << config.port << "\n";
    std::cout << "已注册的路由数: " << server.GetRouter().RouteCount() << "\n";
    std::cout << "========================================\n";
    std::cout << "请打开浏览器访问: http://127.0.0.1:" << config.port << "\n";
    std::cout << "或使用 curl 测试 API:\n";
    std::cout << "  curl http://127.0.0.1:" << config.port << "/api/status\n";
    std::cout << "  curl http://127.0.0.1:" << config.port << "/api/hello?name=Alice\n";
    std::cout << "  curl -X POST http://127.0.0.1:" << config.port << "/api/echo -d 'test data'\n";
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
