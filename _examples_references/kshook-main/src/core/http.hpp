#pragma once

#include <unordered_map>

namespace kshook::http
{
    typedef std::unordered_map<std::string, std::string> headers_t;

    bool init();
    void shutdown();
    void set_global_header(const std::string &header, const std::string &value);
    void enqueue_request(const std::string &url, const headers_t &headers, const std::string &body);
}