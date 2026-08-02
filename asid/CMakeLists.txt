# SidStation ASID plugin - VST3 / AU / Standalone.
#
# Three-voice play by direct SID register control over ASID. JUCE is fetched by
# the root CMakeLists, and the company identity is shared from there.

juce_add_plugin(SidStationAsid
    PRODUCT_NAME              "SidStation ASID"
    COMPANY_NAME              "${SID_COMPANY}"
    BUNDLE_ID                 "no.dehlimusikk.sidstationasid"
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
    AU_MAIN_TYPE              kAudioUnitType_MusicDevice)

target_sources(SidStationAsid PRIVATE
    src/AsidProcessor.cpp
    src/AsidEditor.cpp
    ../common/MidiHub.cpp)

target_include_directories(SidStationAsid PRIVATE ../common src)

target_compile_definitions(SidStationAsid PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SidStationAsid PRIVATE
    sidstation_core
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
