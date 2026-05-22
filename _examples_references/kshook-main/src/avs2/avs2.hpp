#pragma once

#include <unordered_map>

namespace avs2
{
    typedef std::int32_t error_t;

    enum class property_flags
    {
        PROP_XML = 0x000,
        PROP_BINARY = 0x008,
        PROP_JSON = 0x800,
    };

    void resolve_v2173(const char *dll_name);

    extern void *(*property_search)(void *property, void *node, const char *path);
    extern error_t (*property_destroy)(void *property);
    extern error_t (*property_set_flag)(void *property, property_flags flags_set, property_flags flags_unset);
    extern error_t (*property_query_size)(void *property);
    extern error_t (*property_mem_write)(void *property, void *data, std::uint32_t size);
};