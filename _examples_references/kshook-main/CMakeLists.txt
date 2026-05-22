cmake_minimum_required(VERSION 3.20)

project(kshook VERSION 0.1.3.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUNICODE -DJSON_DIAGNOSTICS=1")

find_package(CURL CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

add_executable(${PROJECT_NAME}_launcher src/launcher.rc src/launcher.cpp)
target_include_directories(${PROJECT_NAME}_launcher PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/macros)
set_target_properties(${PROJECT_NAME}_launcher PROPERTIES OUTPUT_NAME "kshook")
set_target_properties(${PROJECT_NAME}_launcher PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")

add_library(${PROJECT_NAME} SHARED
    src/kshook.rc
    src/kshook.cpp
    src/kshook.def
    src/avs2/avs2.cpp
    src/core/log.cpp
    src/core/http.cpp
    src/core/service.cpp
    src/core/version.cpp
    src/common/sdvx/score_save.cpp
    src/common/sdvx/score_export.cpp
    src/game/sv6c/game.cpp
    src/game/sv6c/requests/score_save.cpp
    src/game/sv6c/requests/score_export.cpp
    src/game/sv6c/hooks/property_destroy.cpp
)
configure_file(src/game/gamelist.hpp.in macros/gamelist.hpp @ONLY)
configure_file(src/core/buildinfo.hpp.in macros/buildinfo.hpp @ONLY)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/macros)
target_link_libraries(${PROJECT_NAME} PRIVATE version fmt::fmt CURL::libcurl minhook::minhook magic_enum::magic_enum nlohmann_json::nlohmann_json)