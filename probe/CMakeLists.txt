cmake_minimum_required(VERSION 3.15)
project(sidprobe CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(sidprobe
    main.cpp
    ../core/src/Parameters.cpp
    ../core/src/Patch.cpp)

target_include_directories(sidprobe PRIVATE ../core/include)

if(APPLE)
    target_link_libraries(sidprobe
        "-framework CoreMIDI"
        "-framework CoreFoundation")
endif()
