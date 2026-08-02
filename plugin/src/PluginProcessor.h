// SidStation Editor - plugin processor.
//
// Presents to the DAW as an instrument (MIDI in, audio out). Its automatable
// parameters are generated from the core parameter registry. It talks to the
// SidStation over a MIDI device it opens itself (see MidiHub): changing a
// parameter sends the matching CC, and incoming patch dumps are captured for
// the librarian. The three-voice ASID play lives in the separate ASID plugin.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <optional>
#include <unordered_map>

#include "MidiHub.h"
#include "sidstation/Parameters.h"

class SidStationAudioProcessor : public juce::AudioProcessor,
                                 private juce::AudioProcessorValueTreeState::Listener,
                                 private juce::Timer,
                                 private MidiHub::Listener {
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
    MidiHub& midi() { return midiHub; }

    // Re-sends every parameter's current value to the hardware as DP messages.
    void sendAllParameters();
    // Sends a .syx byte stream (one or more patch dumps) to the unit.
    void sendSyxToUnit(const sidstation::Bytes& data);

    // A patch dump received from the unit, with its exact SysEx bytes.
    struct ReceivedPatch {
        sidstation::Patch patch;
        sidstation::Bytes raw;
    };
    // Returns and clears the most recently received patch (thread-safe), or
    // nullopt if none has arrived since the last call.
    std::optional<ReceivedPatch> takeReceivedPatch();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void midiPatchReceived(const sidstation::Patch& patch,
                           const sidstation::Bytes& raw) override;
    // Builds the MIDI message that sets `p` to `value`: a CC when the parameter
    // has one (the working path on this firmware), otherwise a best-effort
    // Direct-Program SysEx.
    juce::MidiMessage messageForParam(const sidstation::ParamInfo& p, int value);
    void queueParamChange(const sidstation::ParamInfo& info, int value);

    juce::AudioProcessorValueTreeState apvts;
    MidiHub midiHub;

    std::unordered_map<std::string, const sidstation::ParamInfo*> idToInfo;

    // Direct-Program edits queued on the message/automation thread, drained to
    // the MIDI device by the timer (avoids MIDI I/O on the audio thread).
    juce::SpinLock pendingLock;
    juce::Array<juce::MidiMessage> pending;

    // Suppresses DP emission while the host restores state.
    std::atomic<bool> suppressSending{false};

    // Last patch dump received from the unit.
    juce::CriticalSection recvLock;
    std::optional<ReceivedPatch> received;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidStationAudioProcessor)
};
