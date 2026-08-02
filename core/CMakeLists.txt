# The framework-agnostic SidStation protocol library, as a static lib the
# plugin (and anything else) can link. Has no external dependencies.
add_library(sidstation_core STATIC
    src/Parameters.cpp
    src/Patch.cpp
    src/SyxFile.cpp
    src/VoiceEngine.cpp)

target_include_directories(sidstation_core PUBLIC include)
target_compile_features(sidstation_core PUBLIC cxx_std_17)
