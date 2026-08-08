# SidStation ASID plugin - VST3 / AU / Standalone.
#
# Three-voice play by direct SID register control over ASID. JUCE is fetched by
# the root CMakeLists, along with the shared company identity.

juce_add_plugin(SidStationAsid
    PRODUCT_NAME              "SidStation ASID"
    COMPANY_NAME              "${SID_COMPANY}"
    BUNDLE_ID                 "com.dehlimusikk.sidstationasid"
    PLUGIN_MANUFACTURER_CODE  ${SID_MANUFACTURER_CODE}
    PLUGIN_CODE               Asid
    FORMATS                   AU VST3 Standalone
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD   TRUE
    VST3_CATEGORIES           Instrument
    AU_MAIN_TYPE              kAudioUnitType_MusicDevice
    ${SID_ICON})

# Embed assets so they ship inside the plugin binary: the Dehli Musikk logo, plus
# any decorative overlay PNGs present (Overlay.png shared, OverlayVoice1..3.png per
# voice). The overlays are optional - the editor draws them only if embedded.
file(GLOB SID_OVERLAYS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/Overlay*.png")
juce_add_binary_data(SidStationAsidAssets
    SOURCES
        assets/DehliMusikkLogo_PETSCII.png
        assets/SidStationC64.ttf
        ${SID_OVERLAYS})

target_sources(SidStationAsid PRIVATE
    src/AsidProcessor.cpp
    src/AsidEditor.cpp
    src/MidiHub.cpp)

target_include_directories(SidStationAsid PRIVATE src)

target_compile_definitions(SidStationAsid PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SidStationAsid PRIVATE
    sidstation_core
    SidStationAsidAssets
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# JUCE-linked tests (opt in with -DSID_BUILD_TESTS=ON). A console app that builds
# the processor and editor and exercises the parameter layout, plugin state
# round-trip, and preset save/load. The pure protocol logic is tested in core.
option(SID_BUILD_TESTS "Build the JUCE-linked SidStation ASID tests" OFF)
if(SID_BUILD_TESTS)
    juce_add_console_app(SidStationAsidTests PRODUCT_NAME "SidStation ASID Tests")
    target_sources(SidStationAsidTests PRIVATE
        tests/StateTests.cpp
        src/AsidProcessor.cpp
        src/AsidEditor.cpp
        src/MidiHub.cpp)
    target_include_directories(SidStationAsidTests PRIVATE src)
    target_compile_definitions(SidStationAsidTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(SidStationAsidTests PRIVATE
        sidstation_core
        SidStationAsidAssets
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
