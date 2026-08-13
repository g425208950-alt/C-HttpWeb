#pragma once

#include "Router.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

namespace http
{
    // 交互式学习页面的源码白名单：只允许读取这些项目内文件，防止路径穿越读出敏感内容
    inline static const std::unordered_set<std::string> &DocsWhitelist()
    {
        static const std::unordered_set<std::string> set = {
            "main.cpp",
            "ServerConfig.hpp",
            "TCPServer.hpp",
            "ThreadPool.hpp",
            "HTTPServer.hpp",
            "HTTP.hpp",
            "HttpDetail.hpp",
            "Router.hpp",
            "Routes.hpp",
            "DocsRoutes.hpp",
            "CMakeLists.txt",
            "Makefile"};
        return set;
    }

    // 读取项目根目录下某个文件的全部内容，读到失败返回 std::nullopt
    inline static std::optional<std::string> ReadProjectFile(const std::string &name)
    {
        if (DocsWhitelist().find(name) == DocsWhitelist().end())
            return std::nullopt; // 非白名单文件，拒绝
        std::ifstream ifs(name, std::ios::binary);
        if (!ifs.is_open())
            return std::nullopt;
        std::ostringstream oss;
        oss << ifs.rdbuf();
        return oss.str();
    }

    // 注册交互式学习文档相关路由
    //   GET /docs           -> 返回 site/docs.html 学习页面
    //   GET /docs/file      -> 返回 ?name= 指定的源码内容
    inline void RegisterDocsRoutes(Router &router)
    {
        router.Get("/docs", [](const Request & /*req*/)
        {
            std::ifstream ifs("site/docs.html", std::ios::binary);
            Response res;
            if (!ifs.is_open())
            {
                res.status = 404;
                res.reason = "Not Found";
                res.SetTextBody("docs.html not found. Please run server from project root.");
                return res;
            }
            std::ostringstream oss;
            oss << ifs.rdbuf();
            res.status = 200;
            res.reason = "OK";
            res.SetHtmlBody(oss.str());
            return res;
        });

        router.Get("/docs/file", [](const Request &req)
        {
            auto name_opt = req.GetQueryParam("name");
            Response res;
            res.status = 200;
            res.reason = "OK";

            if (!name_opt.has_value() || name_opt.value().empty())
            {
                res.status = 400;
                res.reason = "Bad Request";
                res.SetJsonBody("{\"status\":400,\"msg\":\"missing 'name' query param\"}");
                return res;
            }

            const std::string &name = name_opt.value();
            auto content = ReadProjectFile(name);
            if (!content.has_value())
            {
                res.status = 404;
                res.reason = "Not Found";
                res.SetJsonBody("{\"status\":404,\"msg\":\"file not in whitelist or not readable\"}");
                return res;
            }

            // 以纯文本形式返回原始源码（前端用 highlight.js 着色）
            res.SetTextBody(content.value());
            res.SetHeader("Cache-Control", "no-store");
            return res;
        });
    }

} // namespace http