#pragma once

#include <cstddef>

// 服务器可调参数集中管理，避免散落在各个类构造函数的默认参数里
struct ServerConfig
{
    int port = 8080;
    int backlog = 512;                // listen() 的全连接队列长度
    int read_timeout_ms = 60000;     // keep-alive 空闲超时（毫秒）：无在途请求时，超时即关闭
    int request_deadline_ms = 30000; // 单个请求从首字节起的总时限：防 Slowloris 滴注，滴字节刷不掉
    size_t max_header_bytes = 65536;   // 请求头上限：超限仍找不到 \r\n\r\n 即关闭
    size_t max_body_bytes = 1048576;   // 请求体上限：Content-Length 超限返回 413 并关闭
    size_t max_pending_write = 1048576; // 待发响应上限：客户端读得太慢导致积压超限即关闭
};
