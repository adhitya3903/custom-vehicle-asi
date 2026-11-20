cmake_minimum_required(VERSION 3.15)

# Runtime statis (/MT): wajib sama dengan plugin.lib dari Plugin SDK
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

project(CustomVehicles CXX)

if(NOT DEFINED ENV{PLUGIN_SDK_DIR})
    message(FATAL_ERROR "PLUGIN_SDK_DIR belum di-set (workflow harus mengisinya).")
endif()
file(TO_CMAKE_PATH "$ENV{PLUGIN_SDK_DIR}" SDK)

add_library(CustomVehicles SHARED src/main.cpp)

set_target_properties(CustomVehicles PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
)

# safetyhook.hpp (bagian dari Plugin SDK terbaru) memakai std::expected = C++23
target_compile_options(CustomVehicles PRIVATE /std:c++latest)

# Daftar include & define ini mengikuti proyek open-source yang sudah terbukti
# jalan dengan Plugin SDK (GTA SA 1.0 US) + premake5 milik Plugin SDK sendiri.
target_include_directories(CustomVehicles PRIVATE
    ${SDK}
    ${SDK}/shared
    ${SDK}/shared/game
    ${SDK}/shared/dxsdk
    ${SDK}/shared/rwd3d9
    ${SDK}/injector
    ${SDK}/plugin_sa
    ${SDK}/plugin_sa/game_sa
    ${SDK}/plugin_sa/game_sa/rw
    ${SDK}/plugin_sa/game_sa/enums
)

target_compile_definitions(CustomVehicles PRIVATE
    GTASA
    PLUGIN_SGV_10US
    RW
    _CRT_SECURE_NO_WARNINGS
    _CRT_NON_CONFORMING_SWPRINTFS
)

target_link_directories(CustomVehicles PRIVATE ${SDK}/output/lib)

target_link_libraries(CustomVehicles PRIVATE
    plugin.lib d3d9.lib user32.lib kernel32.lib gdi32.lib shell32.lib advapi32.lib
)
