# SidStation Editor plugin - VST3 / AU / Standalone.
#
# Patch editing over MIDI CC, plus the patch librarian. JUCE is fetched by the
# root CMakeLists, and the company identity is shared from there.

juce_add_plugin(SidStationEditor
    PRODUCT_NAME              "SidStation Editor"
    COMPANY_NAME              "${SID_COMPANY}"
    BUNDLE_ID                 "com.dehlimusikk.sidstationeditor"
    PLUGIN_MANUFACTURER_CODE  ${SID_MANUFACTURER_CODE}
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
    ../common/MidiHub.cpp)

target_include_directories(SidStationEditor PRIVATE ../common)

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
