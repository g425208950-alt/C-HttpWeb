#pragma once

#include <cstddef>

// 服务器可调参数集中管理，避免散落在各个类构造函数的默认参数里
struct ServerConfig
{
    int port = 8080;
    int backlog = 10;                // listen() 的全连接队列长度
    size_t worker_threads = 128;     // 处理连接的线程池大小，决定同时能撑住多少条长连接
    int read_timeout_ms = 60000;     // 连接空闲超时（毫秒）：Reactor 定期扫描，超时即关闭
};
