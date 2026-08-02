// SidStation ASID - plugin processor.
//
// A three-voice instrument that drives the SidStation's SID chip directly over
// ASID. One instance controls one SID voice (chosen per track), so notes on the
// track play that voice with its own frequency and gate. Sends go out over a
// MIDI device the plugin opens itself (MidiHub), not via DAW routing.
//
// Author: Benjamin Dehli. Company: DehliMusikk.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "AsidShared.h"
#include "MidiHub.h"
#include "sidstation/AsidVoicePlayer.h"

class AsidProcessor : public juce::AudioProcessor,
                      private juce::Timer,
                      private juce::AudioProcessorValueTreeState::Listener,
                      private AsidShared::Client {
public:
    AsidProcessor();
    ~AsidProcessor() override;

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SidStation ASID"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
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

    // (Re)sends the full ASID state to the unit. The editor calls this when the
    // MIDI output is opened, so the current sound is pushed to a fresh device.
    // The plugin always streams ASID, there is no on/off, since that is its job.
    void requestReinit() { initRequest.store(true); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void timerCallback() override;
    void queueAsid(const sidstation::Bytes& asidMessage);
    // Reads the voice/filter parameters and sends any that changed (flushed).
    void applyControlChanges(int voice, bool forceAll);
    int paramInt(const char* id) const;

    // Cross-instance sync of the shared filter and volume.
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void sharedUpdated() override;
    void setParamValue(const char* id, int value);

    juce::AudioProcessorValueTreeState apvts;
    MidiHub midiHub;
    sidstation::AsidVoicePlayer asidPlayer;
    std::atomic<bool> initRequest{true};  // send full state on first block / device open

    juce::SpinLock pendingLock;
    juce::Array<juce::MidiMessage> pending;

    // Last control values sent, for change detection on the audio thread.
    struct Sent {
        int voice = -1, wave = -1, attack = -1, decay = -1, sustain = -1, release = -1;
        int pw = -1, sync = -1, ring = -1, route = -1;
        int cutoff = -1, resonance = -1, mode = -1, volume = -1;
    } sent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsidProcessor)
};
