# Top-level build for the DehliMusikk SidStation plugins.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# Builds two plugins that share the core protocol library and the MIDI layer:
#   plugin/  SidStation Editor  (patch editing over CC, and the librarian)
#   asid/    SidStation ASID    (three-voice play, direct SID register control)
#
# Author: Benjamin Dehli. Company: DehliMusikk.
cmake_minimum_required(VERSION 3.22)

project(DehliMusikkSidStation VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared plugin identity. The company name is what groups both plugins under
# DehliMusikk in a DAW's plugin list, and both share one manufacturer code.
set(SID_COMPANY "DehliMusikk")
set(SID_MANUFACTURER_CODE "Dhmk")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

add_subdirectory(core)
add_subdirectory(plugin)
add_subdirectory(asid)
