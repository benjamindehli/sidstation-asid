// SidStation ASID - plugin editor.
//
// A small panel: pick the MIDI output, pick which SID voice this instance
// drives, and toggle ASID play. Notes on the track then play that voice.
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
    void refreshDevices();

    AsidProcessor& proc;

    juce::Label title{{}, "SidStation ASID"};
    juce::Label outLabel{{}, "MIDI Out:"};
    juce::ComboBox outputBox;
    juce::TextButton refreshButton{"Refresh"};
    juce::Label voiceLabel{{}, "SID Voice:"};
    juce::ComboBox voiceBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> voiceAtt;
    juce::ToggleButton asidButton{"ASID Play"};
    juce::Label help;

    juce::Array<juce::MidiDeviceInfo> outDevices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsidEditor)
};
