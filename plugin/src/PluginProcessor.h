// SidStation Editor - plugin processor.
//
// The plugin presents to the DAW as an instrument (MIDI in, audio out). Its
// automatable parameters are generated directly from the core protocol
// library's parameter registry. It talks to the SidStation over a MIDI device
// it opens itself (see MidiHub), not via DAW routing: changing a parameter
// emits the matching Direct-Program SysEx, and incoming patch dumps are
// captured for the librarian.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <optional>
#include <unordered_map>

#include "MidiHub.h"
#include "sidstation/Parameters.h"
#include "sidstation/VoiceEngine.h"

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

    // Three voice play mode. When on, incoming MIDI on channels 1, 2, 3 drives
    // oscillators 1, 2, 3 as separate monophonic voices.
    void setVoicePlayEnabled(bool on) { voicePlayEnabled.store(on); }
    bool isVoicePlayEnabled() const { return voicePlayEnabled.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void midiPatchReceived(const sidstation::Patch& patch,
                           const sidstation::Bytes& raw) override;
    void queueDirectProgram(const sidstation::ParamInfo& info, int value);
    void renderVoiceAction(const sidstation::VoiceAction& action);

    juce::AudioProcessorValueTreeState apvts;
    MidiHub midiHub;

    // Three voice play state. The engine runs on the audio thread in
    // processBlock. oscPitch caches the OSC_TRACK parameter for each oscillator.
    sidstation::VoiceEngine voiceEngine;
    std::atomic<bool> voicePlayEnabled{false};
    const sidstation::ParamInfo* oscPitch[3]{nullptr, nullptr, nullptr};

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
