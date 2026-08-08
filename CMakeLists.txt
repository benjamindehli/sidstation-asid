# Top-level build for the DehliMusikk SidStation ASID plugin.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# Builds on top of the core protocol library:
#   asid/    SidStation ASID    (three-voice play, direct SID register control)
#
# Author: Benjamin Dehli. Company: DehliMusikk.
cmake_minimum_required(VERSION 3.22)

project(DehliMusikkSidStation VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Plugin identity. The company name groups the plugin under DehliMusikk in a
# DAW's plugin list.
set(SID_COMPANY "DehliMusikk")
set(SID_MANUFACTURER_CODE "Dhmk")

# App icon for the Standalone app (JUCE generates the .icns / .ico from it).
# Guarded so the build still works when the file is absent. Passed to
# juce_add_plugin as ${SID_ICON}.
set(SID_ICON "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asid/assets/AppIcon.png")
    set(SID_ICON ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/asid/assets/AppIcon.png")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

add_subdirectory(core)
add_subdirectory(asid)
