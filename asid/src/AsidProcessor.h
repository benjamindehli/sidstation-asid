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

#include "MidiHub.h"
#include "sidstation/AsidVoicePlayer.h"

class AsidProcessor : public juce::AudioProcessor, private juce::Timer {
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

    // Requests are posted here and applied on the audio thread, so the player is
    // only ever touched there (1 = start, 2 = stop).
    void setAsidMode(bool on) { asidRequest.store(on ? 1 : 2); }
    bool isAsidMode() const { return asidMode.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void timerCallback() override;
    void queueAsid(const sidstation::Bytes& asidMessage);

    juce::AudioProcessorValueTreeState apvts;
    MidiHub midiHub;
    sidstation::AsidVoicePlayer asidPlayer;
    std::atomic<bool> asidMode{false};
    std::atomic<int> asidRequest{0};

    juce::SpinLock pendingLock;
    juce::Array<juce::MidiMessage> pending;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsidProcessor)
};
