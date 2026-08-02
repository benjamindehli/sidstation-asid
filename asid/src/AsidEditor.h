// SidStation ASID - plugin editor.
//
// Pick the MIDI output and which SID voice this instance drives, toggle ASID
// play, and shape the voice: waveform, envelope, pulse width, and the filter.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>

#include "AsidProcessor.h"

class AsidEditor : public juce::AudioProcessorEditor {
public:
    explicit AsidEditor(AsidProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void refreshDevices();
    void setupKnob(juce::Slider&, juce::Label&, const juce::String& name,
                   const juce::String& paramId, std::unique_ptr<SliderAtt>&);

    AsidProcessor& proc;
    juce::AudioProcessorValueTreeState& state;

    juce::Label title{{}, "SidStation ASID"};
    juce::Label outLabel{{}, "MIDI Out:"};
    juce::ComboBox outputBox;
    juce::TextButton refreshButton{"Refresh"};
    juce::Label voiceLabel{{}, "SID Voice:"};
    juce::ComboBox voiceBox;
    std::unique_ptr<ComboAtt> voiceAtt;

    juce::Label voiceHeading{{}, "PER VOICE"};
    juce::Label sharedHeading{{}, "SHARED (all voices)"};

    // Per-voice sound.
    juce::Label waveLabel{{}, "Waveform:"};
    juce::ComboBox waveformBox;
    std::unique_ptr<ComboAtt> waveformAtt;
    juce::ToggleButton syncButton{"Sync"}, ringButton{"Ring"};
    std::unique_ptr<ButtonAtt> syncAtt, ringAtt;
    juce::Slider attackKnob, decayKnob, sustainKnob, releaseKnob, pwKnob;
    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel, pwLabel;
    std::unique_ptr<SliderAtt> attackAtt, decayAtt, sustainAtt, releaseAtt, pwAtt;
    juce::ToggleButton routeButton{"Route Through Filter"};
    std::unique_ptr<ButtonAtt> routeAtt;

    // Shared across all three voices.
    juce::Slider cutoffKnob, resKnob, volumeKnob;
    juce::Label cutoffLabel, resLabel, volumeLabel;
    std::unique_ptr<SliderAtt> cutoffAtt, resAtt, volumeAtt;
    juce::Label modeLabel{{}, "Mode:"};
    juce::ComboBox filterModeBox;
    std::unique_ptr<ComboAtt> filterModeAtt;

    juce::Array<juce::MidiDeviceInfo> outDevices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsidEditor)
};
