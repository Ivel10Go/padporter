cmake_minimum_required(VERSION 3.22)

project(PadPorter VERSION 1.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static MSVC runtime: the plugin and app run without a VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/utf-8 /permissive- /Zc:__cplusplus /MP)
endif()

option(PADPORTER_BUILD_TESTS  "Build the core unit tests"          ON)
option(PADPORTER_BUILD_PLUGIN "Build the JUCE standalone app + VST3" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(core)
add_subdirectory(cli)

if(PADPORTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PADPORTER_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()
