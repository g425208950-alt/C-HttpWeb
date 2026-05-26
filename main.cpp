#include "TCPServer.hpp" // 💡 引入你的 TCP 服务器
#include "HTTP.hpp"      // 💡 引入你刚刚读完的 HTTP 协议组件
#include <iostream>

// 💡 这是一个符合 std::function<void(int)> 签名的普通函数
void HttpConnectionHandler(int client_fd) {
    char buf[4096] = {0};
    
    // 1. 【物理读取】从网卡缓冲区把浏览器发来的原始 TCP 字符串捞出来
    int bytes_received = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (bytes_received <= 0) {
        return; // 对端关闭或出错，直接返回（外层 detach 的线程会自动关闭 fd）
    }
    
    std::string raw_request(buf, bytes_received);
    std::cout << "--- 收到浏览器的原始请求 ---\n" << raw_request << "\n---------------------\n";

    // 2. 【应用层解析】调用你写好的静态工厂函数
    auto req_opt = http::Request::Parse(raw_request);
    
    // 3. 【业务处理】构建响应对象
    http::Response res;
    
    if (!req_opt.has_value()) {
        // 如果浏览器发过来的数据不合法，回 400 错误
        res.status = 400;
        res.reason = "Bad Request";
        res.body = "<h1>400 Bad Request</h1>";
    } else {
        // 解析成功，拿到结构化的请求对象
        const http::Request& req = req_opt.value();
        
        // 💡 简易路由分发：根据请求的路径（path）决定回什么网页
        if (req.path == "/" || req.path == "/index.html") {
            res.status = 200;
            res.reason = "OK";
            res.SetHeader("Content-Type", "text/html; charset=utf-8"); // 告诉浏览器这是网页，带上utf-8防止中文乱码
            res.body = "<html>"
                       "<head><title>C++ Web Server</title></head>"
                       "<body>"
                       "    <h1>🚀 焊接成功！</h1>"
                       "    <p>这是一个由 C++ 搭建的 HTTP 服务器。</p>"
                       "</body>"
                       "</html>";
        } else if (req.path == "/api/status") {
            // 顺手做个动态 API 的雏形
            res.status = 200;
            res.reason = "OK";
            res.SetHeader("Content-Type", "application/json");
            res.body = "{\"status\":\"running\",\"msg\":\"Server is healthy\"}";
        } else {
            // 访问了不存在的路径，回 404
            res.status = 404;
            res.reason = "Not Found";
            res.SetHeader("Content-Type", "text/html; charset=utf-8");
            res.body = "<h1>404 页面被怪兽吃掉了！</h1>";
        }
    }

    // 4. 【打包回传】将 Response 对象转成标准 HTTP 文本，拍回浏览器
    std::string raw_response = res.ToString();
    send(client_fd, raw_response.c_str(), raw_response.size(), 0);
    
    // 💡 注意：当前你的底层代码在 AcceptLoop 里已经写了 ::close(client_fd);
    // 所以这里不需要再手动 close，函数退出后，外层的 Lambda 会帮你安全关闭。
}

int main() {
    const int PORT = 8080;
    
    // 1. 初始化你的通用 TCP 服务器实例，监听 8080 端口
    TCPServer server(PORT);
    
    // 2. 【核心焊接点】把业务层的 HttpConnectionHandler 注入到底层框架中
    // 底层 AcceptLoop 捞到 client_fd 后，会自动通过 std::function 调用这个函数
    server.SetConnectionHandler(HttpConnectionHandler);
    
    std::cout << "HTTP 服务器正在启动，监听端口: " << PORT << " ...\n";
    std::cout << "请打开浏览器访问: http://127.0.0.1:" << PORT << "\n";
    
    // 3. 启动服务器（内部会开启 AcceptLoop 后台子线程）
    if (!server.Start()) {
        std::cerr << "服务器启动失败！可能是端口被占用了。\n";
        return -1;
    }
    
    // 4. 防止主线程立刻退出
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}