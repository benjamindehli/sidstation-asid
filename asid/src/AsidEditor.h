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
    // Enable/disable controls that only apply in some states (pulse-wave-only
    // pulse width, sync-vs-free rate). Driven from the timer.
    void updateEnablement();
    void setupKnob(juce::Slider&, juce::Label&, const juce::String& name,
                   const juce::String& paramId, std::unique_ptr<SliderAtt>&);

    // One reusable block of controls for a single LFO target.
    struct LfoControls {
        juce::ToggleButton enableButton{"On"};
        std::unique_ptr<ButtonAtt> enableAtt;
        juce::Label shapeLabel{{}, "Shape:"};
        juce::ComboBox shapeBox;
        juce::ToggleButton syncButton{"Tempo Sync"};
        juce::ComboBox divBox, updateBox;
        juce::Label divLabel{{}, "Sync:"}, updateLabel{{}, "Update:"};
        juce::Slider rateKnob, depthKnob;
        juce::Label rateLabel, depthLabel;
        std::unique_ptr<ComboAtt> shapeAtt, divAtt, updateAtt;
        std::unique_ptr<ButtonAtt> syncAtt;
        std::unique_ptr<SliderAtt> rateAtt, depthAtt;
    };
    void setupLfo(LfoControls&, const juce::String& prefix);
    void layoutLfo(LfoControls&, juce::Rectangle<int> area);

    AsidProcessor& proc;
    juce::AudioProcessorValueTreeState& state;

    juce::Label title{{}, "SidStation ASID"};
    juce::Label outLabel{{}, "MIDI Out:"};
    juce::ComboBox outputBox;
    juce::TextButton refreshButton{"Refresh"};
    juce::Label voiceLabel{{}, "SID Voice:"};
    juce::ComboBox voiceBox;
    std::unique_ptr<ComboAtt> voiceAtt;

    juce::Label settingsHeading{{}, "SETTINGS"};
    juce::Label oscHeading{{}, "OSCILLATOR"};
    juce::Label ampHeading{{}, "AMP"};
    juce::Label sharedHeading{{}, "SHARED (all voices)"};
    juce::Label pitchLfoHeading{{}, "PITCH MOD"}, pwLfoHeading{{}, "PW MOD"}, cutLfoHeading{{}, "CUTOFF MOD"};
    LfoControls pitchLfoUi, pwLfoUi, cutLfoUi;

    // Oscillator.
    juce::Label waveLabel{{}, "Waveform:"};
    juce::ComboBox waveformBox;
    std::unique_ptr<ComboAtt> waveformAtt;
    juce::ToggleButton syncButton{"Sync"}, ringButton{"Ring"};
    std::unique_ptr<ButtonAtt> syncAtt, ringAtt;
    juce::Slider pwKnob, coarseKnob, fineKnob, portaKnob;
    juce::Label pwLabel, coarseLabel, fineLabel, portaLabel;
    std::unique_ptr<SliderAtt> pwAtt, coarseAtt, fineAtt, portaAtt;
    juce::Label portaTrigLabel{{}, "Glide:"}, portaTypeLabel{{}, "Type:"};
    juce::ComboBox portaTrigBox, portaTypeBox;
    std::unique_ptr<ComboAtt> portaTrigAtt, portaTypeAtt;

    // Amp envelope.
    juce::Slider attackKnob, decayKnob, sustainKnob, releaseKnob;
    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel;
    std::unique_ptr<SliderAtt> attackAtt, decayAtt, sustainAtt, releaseAtt;

    // Shared across all three voices. Filter Active is one checkbox per voice
    // (the instance's own voice is highlighted); filter Mode bits combine.
    juce::Label filterActiveLabel{{}, "Filter:"};
    juce::ToggleButton filtButtons[3];
    std::unique_ptr<ButtonAtt> filtAtts[3];
    juce::Label filterModeLabel{{}, "Mode:"};
    juce::ToggleButton modeButtons[3];  // LP, BP, HP
    std::unique_ptr<ButtonAtt> modeAtts[3];
    int highlightedVoice = -1;  // which filter checkbox is currently marked as ours
    juce::Slider cutoffKnob, resKnob, volumeKnob, latencyKnob;
    juce::Label cutoffLabel, resLabel, volumeLabel, latencyLabel;
    std::unique_ptr<SliderAtt> cutoffAtt, resAtt, volumeAtt, latencyAtt;

    juce::Array<juce::MidiDeviceInfo> outDevices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsidEditor)
};
