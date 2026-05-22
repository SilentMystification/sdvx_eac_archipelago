#pragma once

#include <windows.h>
#include <fmt/core.h>

namespace kshook
{
    class formatted_exception : public std::exception
    {
      public:
        template <typename... Args>
        explicit formatted_exception(fmt::format_string<Args...> fmt, Args &&...args)
            : _message(fmt::format(fmt, static_cast<Args &&>(args)...))
        {
        }

        const char *what() const noexcept override
        {
            return _message.c_str();
        };

      private:
        std::string _message = {};
    };

    class win32_exception : public formatted_exception
    {
      public:
        explicit win32_exception(const char *message) : formatted_exception("{} ({})", message, GetLastError())
        {
        }

        template <typename... Args>
        explicit win32_exception(fmt::format_string<Args...> fmt, Args &&...args)
            : formatted_exception("{} ({})", fmt::format(fmt, static_cast<Args &&>(args)...), GetLastError())
        {
        }
    };
}