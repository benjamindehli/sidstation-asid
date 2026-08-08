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
#include "sidstation/WaveTable.h"

class AsidProcessor : public juce::AudioProcessor,
                      private juce::AudioProcessorValueTreeState::Listener,
                      private AsidShared::Client {
public:
    static constexpr int kWtSteps = 8;  // wavetable step count

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
    MidiHub& midi() { return AsidShared::get().out; }  // one output shared by all instances
    int wtStep() const { return wtStepDisplay.load(std::memory_order_relaxed); }  // playing step, -1 = none
    // Host tempo the editor should display: >0 is the DAW BPM (shown read-only),
    // 0 means no host transport (standalone), so the editor shows an editable field.
    double hostBpm() const { return hostBpmValue.load(std::memory_order_relaxed); }
    // True if another instance is currently driving SID voice v (0..2). The editor's
    // voice switch marks voices it shares live with another instance.
    bool voiceUsedByOthers(int v) const;

    // (Re)sends the full ASID state to the unit. The editor calls this when the
    // MIDI output is opened, so the current sound is pushed to a fresh device.
    // The plugin always streams ASID, there is no on/off, since that is its job.
    void requestReinit() { initRequest.store(true); }

    // Manual all-notes-off for every voice (Panic button).
    void panic() { AsidShared::get().panicAllVoices(); }
    // Reset this voice's sound to its default patch. Leaves the shared filter and
    // volume (they belong to every voice) and this instance's identity (asidVoice).
    void resetVoiceToDefault();

    // Voice-sound presets (the same parameter set Init resets), stored as XML files
    // in a user folder. Not the shared filter/volume, voice selection, or tempo.
    juce::File presetsDir() const;
    juce::StringArray presetNames() const;
    void savePreset(const juce::String& name);
    bool loadPreset(const juce::String& name);
    void deletePreset(const juce::String& name);
    juce::String currentPreset() const { return currentPresetName; }

private:
    // One modulation stream per LFO target: its LFO, when it last sent, and the
    // last frame sent (to skip identical steps).
    struct ModStream {
        sidstation::Lfo lfo;
        double lastMs = 0.0;
        sidstation::Bytes lastFrame;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    // Sends one ASID frame to the device now (from processBlock). Used for the
    // start state and control edits, which are not note-timing critical.
    // sendTimeMs < 0 means "now"; the modulation passes the note-aligned time so
    // its frames play on the same timeline as the notes (never desynced when a
    // track is rendered ahead of the playhead).
    void sendAsid(const sidstation::Bytes& asidMessage, double sendTimeMs = -1.0);
    // Adds one ASID frame to a buffer at a sample offset within the block.
    void addFrame(juce::MidiBuffer& out, const sidstation::Bytes& frame, int samplePos);
    // Turns the block's note events into timed ASID frames and sends them. Holds
    // an attack (gate 0->1) back if the gate has not been low long enough for the
    // SidStation to retrigger, so fast repeats on one voice do not go silent.
    void scheduleNotes(const juce::MidiBuffer& midiMessages, int voice);
    // Reads the voice/filter parameters and sends any that changed (flushed).
    void applyControlChanges(int voice, bool forceAll);
    // Runs the three plugin-side LFOs (pitch, pulse width, cutoff) and streams
    // each target register at its own modest rate while active. The SID has none
    // of its own. blockHasNotes holds the pitch stream off note-event blocks so a
    // frequency write does not collide with a note-off.
    void updateModulation(int voice, bool blockHasNotes);
    // Advances one LFO by dt and returns its bipolar value (no rate gate).
    double sampleLfo(sidstation::Lfo&, const juce::String& prefix, double dt,
                     bool playing, double ppq, double bpm);
    int paramInt(const char* id) const;
    int paramInt(const juce::String& id) const { return paramInt(id.toRawUTF8()); }
    // 3-bit masks from the shared filter toggles: routing (voice 1/2/3) and mode
    // (bit0 LP, bit1 BP, bit2 HP, combinable).
    int routingMask() const {
        return (paramInt("filt1") ? 1 : 0) | (paramInt("filt2") ? 2 : 0) | (paramInt("filt3") ? 4 : 0)
             | (paramInt("filtExt") ? 8 : 0);  // bit 3: external input through the filter
    }
    int modeMask() const {
        return (paramInt("modeLP") ? 1 : 0) | (paramInt("modeBP") ? 2 : 0) | (paramInt("modeHP") ? 4 : 0)
             | (paramInt("voice3off") ? 8 : 0);  // bit 3: voice 3 output off (silent mod source)
    }
    // SID control-register waveform bits from the four toggles. The waveforms
    // combine (bits OR together), but noise locks the others on the 6581, so when
    // noise is on it wins alone. 0 = no waveform (a silent voice).
    int waveBits() const {
        return sidstation::sid::waveformBits(paramInt("waveTri"), paramInt("waveSaw"),
                                             paramInt("wavePulse"), paramInt("waveNoise"));
    }
    // Same, for one wavetable step's four toggles (noise exclusive).
    int wtStepWaveBits(int step) const {
        const juce::String s(step);
        return sidstation::sid::waveformBits(paramInt("wtTri" + s), paramInt("wtSaw" + s),
                                             paramInt("wtPulse" + s), paramInt("wtNoise" + s));
    }
    float paramFloat(const char* id) const;
    float paramFloat(const juce::String& id) const { return paramFloat(id.toRawUTF8()); }

    // Cross-instance sync of the shared filter and volume.
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void sharedUpdated() override;
    void setParamValue(const char* id, int value);
    // Recompute the player's pitch offset from coarse + fine + the pitch wheel
    // (scaled by the bend range). Cheap: sets a value the frequency stream reads.
    void updatePitchOffset();
    // Effective 0..1 modulation amount for an LFO: its depth, scaled by the mod
    // wheel when its Mod Wheel toggle is on, and ramped in over its fade-in delay.
    double lfoAmount(const char* prefix, double nowMs) const;
    int pitchWheelValue = 8192;  // last MIDI pitch wheel value (14-bit, centre 8192)
    int modWheelValue = 0;       // last MIDI mod wheel (CC 1) value, 0..127
    double noteOnMs = -1.0e18;   // last note attack time (wall clock), for the LFO fade-in
    // Currently-playing wavetable step (-1 = not playing), for the editor's display.
    std::atomic<int> wtStepDisplay{-1};
    // Host BPM for the editor's tempo field (0 = no host transport / standalone).
    std::atomic<double> hostBpmValue{0.0};

    juce::AudioProcessorValueTreeState apvts;
    sidstation::AsidVoicePlayer asidPlayer;
    std::atomic<bool> initRequest{true};  // send full state on first block / device open
    int lastOutGeneration = 0;            // shared-device generation this instance has pushed for

    // Note scheduling state (this instance's single voice).
    static constexpr double kMaxScheduleAheadMs = 500.0;  // sane alignment ceiling (> lookahead)
    static constexpr double kSettleMs = 15.0;             // trailing flush after a note-off under pitch mod
    static constexpr double kHardRestartMs = 16.0;        // envelope drain window before a re-attack
    double voiceClockMs = 0.0;    // target time of the last frame sent, keeps order
    double lastGateOffMs = -1.0e18;  // when this voice last released, to time hard restarts
    int lastPlaying = 0;          // transport state last block, to spot a start
    int lastModPlaying = 0;       // transport state last block in updateModulation, to spot a stop
    double lastPlayheadMs = 0.0;  // playhead last block, to spot a jump
    int lastReleaseGen = 0;       // shared watchdog release generation seen, to clear a stale note
    juce::String currentPresetName;  // last saved/loaded preset, for the editor's display
    // A voice-sound parameter (in a preset, and reset by Init): not a shared global,
    // not the voice selection, not the tempo.
    static bool isVoiceSoundParam(const juce::String& id) {
        return !AsidShared::isShared(id) && id != "asidVoice" && id != "bpm";
    }

    ModStream pitchStream, pwStream, cutStream;  // only the .lfo of each is used now
    sidstation::WaveTablePlayer wtPlayer;
    double modTickMs = 0.0;       // one modulation clock for the whole voice
    int wtArp = 0;                // current wavetable arpeggio offset (semitones)
    bool wtOwnsWave = false;      // wavetable is driving the waveform register
    bool lastWtOn = false;        // wtOn last block, to catch the switch-off edge
    double glidePitch = -1.0;     // current sounding pitch (fractional note); -1 = no note
    bool lfoOwnedPw = false;      // PW LFO drives pulse width, skip the static send
    bool lfoOwnedCutoff = false;  // cutoff LFO drives the shared cutoff, skip the static send

    // Coalesce control sends so a knob drag cannot flood the MIDI port and jitter
    // the notes. Applies to everything except the forced full-state push.
    static constexpr double kControlIntervalMs = 16.0;  // ~60 Hz
    double lastControlMs = 0.0;

    // Last shared value synced in from another instance. A shared change only
    // needs sending by the instance where it actually happened (the one filter
    // is common), so a value that matches its echo is skipped here.
    std::atomic<int> echoCutoff{-1}, echoResonance{-1}, echoMode{-1}, echoRouting{-1}, echoVolume{-1};

    // Last control values sent, for change detection on the audio thread. routing
    // and mode are 3-bit masks (routing: voice 1/2/3; mode: LP/BP/HP, combinable).
    struct Sent {
        int voice = -1, wave = -1, attack = -1, decay = -1, sustain = -1, release = -1;
        int pw = -1, sync = -1, ring = -1, test = -1, routing = -1, coarse = -100, fine = -100;
        int cutoff = -1, resonance = -1, mode = -1, volume = -1, bendRange = -1;
    } sent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsidProcessor)
};
