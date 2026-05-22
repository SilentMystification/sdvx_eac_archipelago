cmake_minimum_required(VERSION 3.20)
project(sdvx_archipelago CXX)

set(CMAKE_CXX_STANDARD 17)

# ---- MinHook (included as source) ------------------------------------------
add_library(minhook STATIC
    MinHook/src/buffer.c
    MinHook/src/hook.c
    MinHook/src/trampoline.c
    MinHook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC MinHook/include)

# ---- Main DLL --------------------------------------------------------------
add_library(sdvx_archipelago SHARED
    src/dllmain.cpp
    src/config.cpp
    src/ap_client.cpp
    src/hooks.cpp
)

target_include_directories(sdvx_archipelago PRIVATE
    src
    include          # for json.hpp
)

target_link_libraries(sdvx_archipelago PRIVATE
    minhook
    winhttp
    ws2_32
)

# Must be built as x64
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Must build as 64-bit (x64)")
endif()

# Output as d3d9.dll so it acts as a proxy
set_target_properties(sdvx_archipelago PROPERTIES
    OUTPUT_NAME  "d3d9"
    SUFFIX       ".dll"
    PREFIX       ""
)

# Linker flags to export our d3d9 symbols
target_link_options(sdvx_archipelago PRIVATE
    /DEF:${CMAKE_SOURCE_DIR}/src/exports.def
)
