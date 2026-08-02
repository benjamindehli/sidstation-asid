# SidStation Editor plugin — VST3 / AU / Standalone via JUCE.
#
# JUCE is pulled in with FetchContent so there's nothing to commit or manage as
# a submodule. Bump GIT_TAG to move to a newer JUCE release.
include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15         # pinned; adjust as desired
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SidStationEditor
    PRODUCT_NAME              "SidStation Editor"
    COMPANY_NAME              "Benjamin Dehli"
    BUNDLE_ID                 "no.arkitektum.sidstationeditor"
    PLUGIN_MANUFACTURER_CODE  Bdli
    PLUGIN_CODE               Sid1
    FORMATS                   AU VST3 Standalone
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         TRUE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD   TRUE
    VST3_CATEGORIES           Instrument
    AU_MAIN_TYPE              kAudioUnitType_MusicDevice)

target_sources(SidStationEditor PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/MidiHub.cpp)

target_compile_definitions(SidStationEditor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SidStationEditor PRIVATE
    sidstation_core
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
