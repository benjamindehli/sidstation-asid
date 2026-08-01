// SidStation Editor — plugin processor.
//
// The plugin presents to the DAW as an instrument (MIDI in, audio out). Its
// automatable parameters are generated directly from the core protocol
// library's parameter registry; changing any parameter emits the corresponding
// Direct-Program SysEx into the plugin's MIDI output, which the DAW routes to
// the SidStation. Incoming MIDI (e.g. notes) is passed through to the hardware.
//
// Scaffold scope (milestone 3): builds VST3/AU/Standalone, exposes every
// parameter, and emits correct DP messages on change. A generic editor is used
// for now; the custom GUI, audio pass-through from the hardware input, and
// patch librarian are later milestones.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <unordered_map>

#include "sidstation/Parameters.h"

class SidStationAudioProcessor : public juce::AudioProcessor,
                                 private juce::AudioProcessorValueTreeState::Listener {
public:
    SidStationAudioProcessor();
    ~SidStationAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SidStation Editor"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() { return apvts; }

    // Re-sends every parameter's current value to the hardware as DP messages
    // (used later by a "send whole patch to unit" action).
    void sendAllParameters();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void queueDirectProgram(const sidstation::ParamInfo& info, int value);

    juce::AudioProcessorValueTreeState apvts;

    // Maps a parameter id to its registry entry for fast lookup on change.
    std::unordered_map<std::string, const sidstation::ParamInfo*> idToInfo;

    // MIDI produced on the message/automation thread, drained in processBlock.
    juce::SpinLock pendingLock;
    juce::Array<juce::MidiMessage> pending;

    // Suppresses DP emission while the host restores state, so loading a project
    // doesn't overwrite the unit's patch with defaults.
    std::atomic<bool> suppressSending{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidStationAudioProcessor)
};
