#include "log.hpp"
#include <fstream>
#include <iostream>
#include <windows.h>
#include <fmt/color.h>
#include <fmt/chrono.h>

auto log_debug = false;
auto log_console = false;
auto log_file = std::ofstream{};

void kshook::log::init(const std::filesystem::path &path, bool debug)
{
    log_debug = debug;

    if (log_debug && AllocConsole() != 0)
    {
        log_console = true;

        FILE *stream = nullptr;

        auto output = GetStdHandle(STD_OUTPUT_HANDLE);
        auto flags = ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;

        SetConsoleMode(output, flags);

        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }

    log_file = std::ofstream(path, std::ofstream::app);
}

void kshook::log::shutdown()
{
    if (log_console)
        FreeConsole();
}

bool kshook::log::is_debug()
{
    return log_debug;
}

void kshook::log::write(const std::string &prefix, const std::string &message)
{
    auto time = std::time(nullptr);
    auto line = fmt::format("{:%Y-%m-%d %H:%M:%S} {:>6}  {}\n", fmt::localtime(time), prefix, message);

    if (log_debug && log_console)
    {
        auto color = fmt::color::gray;

        if (prefix == "INFO")
            color = fmt::color::white;
        else if (prefix == "WARN")
            color = fmt::color::orange;
        else if (prefix == "ERROR")
            color = fmt::color::orange_red;

        std::cerr << fmt::format(fg(color), "{}", line);
    }
    else
    {
        std::cerr << line;
    }

    if (prefix == "DEBUG" && !log_debug)
        return;

    if (!log_file.good())
        return;

    log_file << line;
    log_file.flush();
}