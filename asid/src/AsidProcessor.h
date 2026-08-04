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
#include "sidstation/Lfo.h"

class AsidProcessor : public juce::AudioProcessor,
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
    // Sends one ASID frame to the device now (from processBlock). Used for the
    // start state and control edits, which are not note-timing critical.
    void sendAsid(const sidstation::Bytes& asidMessage);
    // Adds one ASID frame to a buffer at a sample offset within the block.
    void addFrame(juce::MidiBuffer& out, const sidstation::Bytes& frame, int samplePos);
    // Turns the block's note events into timed ASID frames and sends them. Holds
    // an attack (gate 0->1) back if the gate has not been low long enough for the
    // SidStation to retrigger, so fast repeats on one voice do not go silent.
    void scheduleNotes(const juce::MidiBuffer& midiMessages, int voice);
    // Reads the voice/filter parameters and sends any that changed (flushed).
    void applyControlChanges(int voice, bool forceAll);
    // Runs the plugin-side LFO and streams its target register at a modest rate
    // while it is active. Pitch shares the frequency register with the notes, so
    // it is held off on any block that also carries a note event, to keep a pitch
    // write from colliding with a note-off. The SID has no LFOs of its own.
    void updateModulation(int voice, bool blockHasNotes);
    int paramInt(const char* id) const;
    float paramFloat(const char* id) const;

    // Cross-instance sync of the shared filter and volume.
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void sharedUpdated() override;
    void setParamValue(const char* id, int value);

    juce::AudioProcessorValueTreeState apvts;
    MidiHub midiHub;
    sidstation::AsidVoicePlayer asidPlayer;
    std::atomic<bool> initRequest{true};  // send full state on first block / device open

    // Note scheduling state (this instance's single voice).
    static constexpr double kMaxScheduleAheadMs = 500.0;  // sane alignment ceiling (> lookahead)
    static constexpr double kSettleMs = 15.0;             // trailing flush after a note-off under pitch mod
    double voiceClockMs = 0.0;    // target time of the last frame sent, keeps order
    int lastPlaying = 0;          // transport state last block, to spot a start
    double lastPlayheadMs = 0.0;  // playhead last block, to spot a jump

    // Modulation state (this instance's single LFO). The stream interval comes
    // from the LFO update-rate parameter (PAL/NTSC/Eco/Smooth).
    sidstation::Lfo lfo;
    double lastModMs = 0.0;                // last time a modulation frame went out
    sidstation::Bytes lastModFrame;        // last frame sent, to skip identical steps
    bool lfoOwnedPw = false;               // LFO drives pulse width, skip the static send
    bool lfoOwnedCutoff = false;           // LFO drives the shared cutoff, skip the static send

    // Coalesce control sends so a knob drag cannot flood the MIDI port and jitter
    // the notes. Applies to everything except the forced full-state push.
    static constexpr double kControlIntervalMs = 16.0;  // ~60 Hz
    double lastControlMs = 0.0;

    // Last shared value synced in from another instance. A shared change only
    // needs sending by the instance where it actually happened (the one filter
    // is common), so a value that matches its echo is skipped here.
    std::atomic<int> echoCutoff{-1}, echoResonance{-1}, echoMode{-1}, echoVolume{-1};

    // Last control values sent, for change detection on the audio thread.
    struct Sent {
        int voice = -1, wave = -1, attack = -1, decay = -1, sustain = -1, release = -1;
        int pw = -1, sync = -1, ring = -1, route = -1, coarse = -100, fine = -100;
        int cutoff = -1, resonance = -1, mode = -1, volume = -1;
    } sent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsidProcessor)
};
