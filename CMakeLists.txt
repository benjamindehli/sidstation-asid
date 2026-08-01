# Top-level build for the SidStation Editor.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# JUCE is fetched automatically (see plugin/CMakeLists.txt). The pure-C++ core
# library has no dependencies and can also be built/tested on its own via
# core/Makefile. The standalone MIDI probe (probe/) has its own build.
cmake_minimum_required(VERSION 3.22)

project(SidStationEditor VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(core)
add_subdirectory(plugin)
