cmake_minimum_required(VERSION 3.15)

# Static runtime: plugin tidak butuh VC++ Redistributable di PC pemain
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

project(CustomVehicles CXX)

add_library(CustomVehicles SHARED src/main.cpp)

# Hasil build: CustomVehicles.asi (bukan .dll)
set_target_properties(CustomVehicles PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
