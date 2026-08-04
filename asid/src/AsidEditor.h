// SidStation ASID - plugin editor.
//
// Pick the MIDI output and which SID voice this instance drives, and shape the
// sound: per-voice waveform, envelope, pulse width, sync and ring, plus the
// shared filter, volume and output latency.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>

#include "AsidProcessor.h"
#include "SidLookAndFeel.h"

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
    void setupKnob(juce::Component& parent, juce::Slider&, juce::Label&, const juce::String& name,
                   const juce::String& paramId, std::unique_ptr<SliderAtt>&);
    void setTab(int t);                                // show one page, hide the others
    void layoutOscPage(juce::Rectangle<int> area);
    void layoutAmpModPage(juce::Rectangle<int> area);
    void layoutSharedPage(juce::Rectangle<int> area);
    void layoutWavePage(juce::Rectangle<int> area);

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
    void setupLfo(juce::Component& parent, LfoControls&, const juce::String& prefix);
    void layoutLfo(LfoControls&, juce::Rectangle<int> area);

    static constexpr int kBorder = 16;  // C64 screen border

    SidLookAndFeel laf;  // declared first so it outlives every child that uses it
    AsidProcessor& proc;
    juce::AudioProcessorValueTreeState& state;

    juce::Label title{{}, "SidStation ASID"};
    // Tab bar and the three pages the tabs switch between.
    juce::TextButton oscTabBtn{"OSC"}, ampModTabBtn{"AMP+MOD"}, sharedTabBtn{"SHARED"}, waveTabBtn{"WAVE"};
    juce::Component oscPage, ampModPage, sharedPage, wtPage;
    int currentTab = 0;

    // MIDI load meter (bytes/sec vs the SidStation's ~3125 B/s ceiling).
    juce::Label midiLoadLabel{{}, "MIDI LOAD"};
    juce::Rectangle<int> meterArea;
    long long lastBytes = 0;
    double lastBytesMs = 0.0;
    float midiLoad = 0.0f;

    juce::Label outLabel{{}, "MIDI Out:"};
    juce::ComboBox outputBox;
    juce::TextButton refreshButton{"Refresh"};
    juce::Label voiceLabel{{}, "SID Voice:"};
    juce::ComboBox voiceBox;
    std::unique_ptr<ComboAtt> voiceAtt;

    // Section boxes drawn as titled group frames, spread across the three tab
    // pages. Filter nests Cutoff Mod. Filter and Master are the shared controls.
    juce::GroupComponent oscGroup, glideGroup, ampGroup;
    juce::GroupComponent pitchModGroup, pwModGroup, filterGroup, cutModGroup, masterGroup;
    LfoControls pitchLfoUi, pwLfoUi, cutLfoUi;

    // Wavetable (WAVE tab): global config plus one row per step.
    juce::GroupComponent wtConfigGroup, wtStepsGroup;
    juce::ToggleButton wtOnButton{"On"};
    std::unique_ptr<ButtonAtt> wtOnAtt;
    juce::Slider wtSpeedKnob, wtLengthKnob, wtLoopKnob;
    juce::Label wtSpeedLabel, wtLengthLabel, wtLoopLabel;
    std::unique_ptr<SliderAtt> wtSpeedAtt, wtLengthAtt, wtLoopAtt;
    juce::Label wtStepNum[AsidProcessor::kWtSteps];
    juce::ComboBox wtWaveBox[AsidProcessor::kWtSteps];
    std::unique_ptr<ComboAtt> wtWaveAtt[AsidProcessor::kWtSteps];
    juce::Slider wtArpSlider[AsidProcessor::kWtSteps];
    std::unique_ptr<SliderAtt> wtArpAtt[AsidProcessor::kWtSteps];

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
