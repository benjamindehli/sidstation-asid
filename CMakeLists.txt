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

project(DehliMusikkSidStation VERSION 1.2.1 LANGUAGES C CXX)

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
    GIT_TAG        9.0.6
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchJuce.cmake)
FetchContent_MakeAvailable(JUCE)

# The JUCE-linked tests and the core library they link, built with the address
# and undefined behaviour sanitizers, for the sanitizers job in CI. A read one
# byte past a buffer hands back a plausible number rather than failing a check,
# and these stop at it. Not for the plugin targets: nothing loads a sanitized
# plugin.
option(SID_SANITIZE "Build the JUCE-linked tests with address and UB sanitizers" OFF)

function(sid_sanitize target)
    if(NOT SID_SANITIZE)
        return()
    endif()
    if(MSVC)
        message(WARNING "SID_SANITIZE does nothing with MSVC")
        return()
    endif()
    # Frame pointers and no tail calls, so a report names every frame it came
    # through.
    target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-optimize-sibling-calls
        -g)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
endfunction()

add_subdirectory(core)
add_subdirectory(asid)
