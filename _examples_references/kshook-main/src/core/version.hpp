#pragma once

namespace kshook::version
{
    std::vector<std::uint8_t> get_info(const std::string &filename = "");
    std::string query_string(const std::vector<std::uint8_t> &block, const std::string &key);
}