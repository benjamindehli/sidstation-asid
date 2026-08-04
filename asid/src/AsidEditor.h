// SidStation ASID - plugin editor.
//
// Pick the MIDI output and which SID voice this instance drives, and shape the
// sound: per-voice waveform, envelope, pulse width, sync and ring, plus the
// shared filter, volume and output latency.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>

#include "AsidProcessor.h"

class AsidEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AsidEditor(AsidProcessor&);
    ~AsidEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void refreshDevices();
    // Pulse width and the LFO only matter on a pulse wave, so grey them out
    // otherwise. Driven from the timer, since the waveform is a plain parameter.
    void updateEnablement();
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
    juce::Slider attackKnob, decayKnob, sustainKnob, releaseKnob, pwKnob, restartKnob;
    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel, pwLabel, restartLabel;
    std::unique_ptr<SliderAtt> attackAtt, decayAtt, sustainAtt, releaseAtt, pwAtt, restartAtt;
    juce::ToggleButton routeButton{"Route Through Filter"};
    std::unique_ptr<ButtonAtt> routeAtt;

    // Shared across all three voices.
    juce::Slider cutoffKnob, resKnob, volumeKnob, latencyKnob;
    juce::Label cutoffLabel, resLabel, volumeLabel, latencyLabel;
    std::unique_ptr<SliderAtt> cutoffAtt, resAtt, volumeAtt, latencyAtt;
    juce::Label modeLabel{{}, "Mode:"};
    juce::ComboBox filterModeBox;
    std::unique_ptr<ComboAtt> filterModeAtt;

    // LFO (per voice, targets pulse width for now).
    juce::Label lfoHeading{{}, "LFO"};
    juce::Label lfoTargetLabel{{}, "Target:"}, lfoShapeLabel{{}, "Shape:"}, lfoDivLabel{{}, "Sync:"};
    juce::Label lfoUpdateLabel{{}, "Update:"};
    juce::ComboBox lfoTargetBox, lfoShapeBox, lfoDivBox, lfoUpdateBox;
    std::unique_ptr<ComboAtt> lfoTargetAtt, lfoShapeAtt, lfoDivAtt, lfoUpdateAtt;
    juce::ToggleButton lfoSyncButton{"Tempo Sync"};
    std::unique_ptr<ButtonAtt> lfoSyncAtt;
    juce::Slider lfoRateKnob, lfoDepthKnob;
    juce::Label lfoRateLabel, lfoDepthLabel;
    std::unique_ptr<SliderAtt> lfoRateAtt, lfoDepthAtt;

    juce::Label diagLabel;  // live host-timing readout, to diagnose sync

    juce::Array<juce::MidiDeviceInfo> outDevices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsidEditor)
};
