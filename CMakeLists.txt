# SPDX-License-Identifier: MIT
# Minimal example CLAP plugins for the Oscairn CLAP extensions (oscairn-clap).
cmake_minimum_required(VERSION 3.17)
project(oscairn-clap-examples VERSION 0.1.0 LANGUAGES CXX)

option(BUILD_TESTING "Build and register the example self-tests" ON)
set(OSCAIRN_PLUGIN_CHECK "" CACHE FILEPATH
    "Path to a built oscairn-plugin-check binary; when set, its checks are registered as the plugin-check CTest label")

include(CTest)
include(FetchContent)
FetchContent_Declare(clap
    GIT_REPOSITORY https://github.com/free-audio/clap
    GIT_TAG 1.2.10
    GIT_SHALLOW TRUE)
set(CLAP_BUILD_TESTS OFF)
FetchContent_MakeAvailable(clap)

add_library(oscairn_clap_headers INTERFACE)
target_include_directories(oscairn_clap_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(oscairn_clap_headers INTERFACE cxx_std_17)

add_subdirectory(examples)
