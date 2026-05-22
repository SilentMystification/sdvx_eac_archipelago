#include <filesystem>
#include "version.hpp"
#include "exception.hpp"

std::vector<std::uint8_t> kshook::version::get_info(const std::string &filename)
{
    auto result = std::vector<std::uint8_t>{};
    auto path = std::filesystem::path(filename);

    if (filename.empty())
    {
        auto base_module = GetModuleHandle(nullptr);

        if (base_module == nullptr)
            throw win32_exception("GetModuleHandle failed");

        auto base_filename = std::string(MAX_PATH, 0);

        if (GetModuleFileNameA(base_module, base_filename.data(), base_filename.size()) == NULL)
            throw win32_exception("GetModuleFileName failed");

        path = std::filesystem::path(base_filename);
    }

    auto handle = DWORD{0};
    auto size = GetFileVersionInfoSizeA(path.string().c_str(), &handle);

    if (size == NULL)
        throw win32_exception("GetFileVersionInfoSizeA failed");

    result.resize(size);

    if (GetFileVersionInfoA(path.string().c_str(), handle, size, result.data()) == NULL)
        throw win32_exception("GetFileVersionInfo failed");

    return result;
}

std::string kshook::version::query_string(const std::vector<std::uint8_t> &block, const std::string &key)
{
    char *result = nullptr;

    if (VerQueryValueA(block.data(), key.c_str(), reinterpret_cast<void **>(&result), nullptr) == NULL)
        throw win32_exception("VerQueryValue failed");

    return result;
}
