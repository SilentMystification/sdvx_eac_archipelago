#pragma once

#include <fmt/core.h>
#include <filesystem>

namespace kshook::log
{
    void init(const std::filesystem::path &path, bool debug);
    void shutdown();
    bool is_debug();
    void write(const std::string &prefix, const std::string &message);

    template <typename... Args> void info(fmt::format_string<Args...> fmt, Args &&...args)
    {
        write("INFO", fmt::format(fmt, static_cast<Args &&>(args)...));
    }
    template <typename... Args> void warn(fmt::format_string<Args...> fmt, Args &&...args)
    {
        write("WARN", fmt::format(fmt, static_cast<Args &&>(args)...));
    }
    template <typename... Args> void debug(fmt::format_string<Args...> fmt, Args &&...args)
    {
        write("DEBUG", fmt::format(fmt, static_cast<Args &&>(args)...));
    }
    template <typename... Args> void error(fmt::format_string<Args...> fmt, Args &&...args)
    {
        write("ERROR", fmt::format(fmt, static_cast<Args &&>(args)...));
    }
}