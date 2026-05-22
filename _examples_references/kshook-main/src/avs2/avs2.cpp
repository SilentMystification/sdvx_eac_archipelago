#include "avs2.hpp"
#include "../core/log.hpp"
#include "../core/exception.hpp"

decltype(avs2::property_search) avs2::property_search = nullptr;
decltype(avs2::property_destroy) avs2::property_destroy = nullptr;
decltype(avs2::property_set_flag) avs2::property_set_flag = nullptr;
decltype(avs2::property_query_size) avs2::property_query_size = nullptr;
decltype(avs2::property_mem_write) avs2::property_mem_write = nullptr;

void resolve(const char *dll_name, const char *version, const std::unordered_map<const char *, void *> &imports)
{
    const auto handle = GetModuleHandleA(dll_name);

    if (handle == nullptr)
        throw kshook::win32_exception("GetModuleHandleA for '{}' failed", dll_name);

    kshook::log::debug("Found module '{}' ({}) at {:#x}.", dll_name, version, std::uintptr_t(handle));

    for (auto &[name, address] : imports)
    {
        auto target = GetProcAddress(handle, name);

        if (target == nullptr)
            throw kshook::win32_exception("GetProcAddress for '{}' failed", name);

        kshook::log::debug("Found import function '{}' at {:#x}.", name, std::uintptr_t(target));

        *reinterpret_cast<void **>(address) = target;
    }
}

void avs2::resolve_v2173(const char *dll_name)
{
    resolve(dll_name, "v2.17.3",
            {
                {"XCgsqzn00000a1", &property_search},
                {"XCgsqzn0000091", &property_destroy},
                {"XCgsqzn000009a", &property_set_flag},
                {"XCgsqzn000009f", &property_query_size},
                {"XCgsqzn00000b8", &property_mem_write},
            });
}