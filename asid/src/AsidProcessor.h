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

#include <atomic>
#include <cmath>

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
    // Returns false if the name is unusable or the file could not be written; the
    // stored name is presetKey(name), which is what currentPreset() then reports.
    bool savePreset(const juce::String& name);
    bool loadPreset(const juce::String& name);
    void deletePreset(const juce::String& name);
    juce::String currentPreset() const { return currentPresetName; }
    // A preset is a file named <key>.xml, so the name has to be a legal filename.
    // juce::File::getChildFile honours "../" and absolute paths, so an unsanitised
    // name could write outside the presets folder and then never show up in the
    // list. createLegalFileName drops the separators that make that reachable, and
    // is idempotent, so applying it twice is harmless.
    static juce::String presetKey(const juce::String& name) {
        return juce::File::createLegalFileName(name.trim());
    }

    // Editor UI preference (persisted with the plugin state): show hover hints.
    bool showTooltips() const { return tooltipsOn; }
    void setShowTooltips(bool on) { tooltipsOn = on; }

private:
    // Cached parameter pointers, resolved once at construction.
    //
    // The audio thread must not build a juce::String (it always heap-allocates) or
    // walk the APVTS parameter map (a std::map<StringRef>, so string compares per
    // lookup). The old paramInt("id") and paramInt(prefix + "Suffix") calls did both,
    // roughly 30 allocations per modulation tick at up to 100 Hz once the wavetable
    // and all three LFOs were live.
    //
    // Safe to hold: APVTS creates one heap ParameterAdapter per parameter during its
    // own construction and never rebuilds them, and replaceState only swaps the
    // ValueTree, so these stay valid across setStateInformation.
    //
    // Named members rather than an id-keyed table, so a mistake is a compile error
    // instead of a parameter that silently reads 0 forever.
    struct ParamPtrs {
        using P = std::atomic<float>*;
        P asidVoice{}, waveTri{}, waveSaw{}, wavePulse{}, waveNoise{};
        P attack{}, decay{}, sustain{}, release{}, pulseWidth{};
        P coarse{}, fine{}, pitchBendRange{};
        P portaTime{}, portaTrigger{}, portaType{};
        P sync{}, ring{}, test{};
        P filt1{}, filt2{}, filt3{}, filtExt{};
        P modeLP{}, modeBP{}, modeHP{}, voice3off{};
        P cutoff{}, resonance{}, volume{};
        P latency{}, modRate{}, bpm{};
        P wtOn{}, wtSpeed{}, wtLength{}, wtLoop{};
        // One block per LFO target and per wavetable step, so the hot paths index in
        // rather than concatenating an id.
        struct Lfo { P on{}, shape{}, sync{}, rate{}, div{}, depth{}, wheel{}, delay{}; };
        Lfo pitchLfo, pwLfo, cutLfo;
        struct Step { P tri{}, saw{}, pulse{}, noise{}, sync{}, ring{}, test{}, pw{}, arp{}; };
        Step wt[kWtSteps];
    };
    ParamPtrs pp;
    void buildParamPtrs();

    // Read a cached parameter: no allocation, no lookup.
    static int paramInt(const std::atomic<float>* p) {
        return p != nullptr ? static_cast<int>(std::lround(p->load())) : 0;
    }
    static float paramFloat(const std::atomic<float>* p) {
        return p != nullptr ? p->load() : 0.0f;
    }

    juce::File presetFile(const juce::String& name) const {
        return presetsDir().getChildFile(presetKey(name) + ".xml");
    }

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
    double sampleLfo(sidstation::Lfo&, const ParamPtrs::Lfo& ids, double dt,
                     bool playing, double ppq, double bpm);
    // 3-bit masks from the shared filter toggles: routing (voice 1/2/3) and mode
    // (bit0 LP, bit1 BP, bit2 HP, combinable).
    int routingMask() const {
        return (paramInt(pp.filt1) ? 1 : 0) | (paramInt(pp.filt2) ? 2 : 0) | (paramInt(pp.filt3) ? 4 : 0)
             | (paramInt(pp.filtExt) ? 8 : 0);  // bit 3: external input through the filter
    }
    int modeMask() const {
        return (paramInt(pp.modeLP) ? 1 : 0) | (paramInt(pp.modeBP) ? 2 : 0) | (paramInt(pp.modeHP) ? 4 : 0)
             | (paramInt(pp.voice3off) ? 8 : 0);  // bit 3: voice 3 output off (silent mod source)
    }
    // SID control-register waveform bits from the four toggles. The waveforms
    // combine (bits OR together), but noise locks the others on the 6581, so when
    // noise is on it wins alone. 0 = no waveform (a silent voice).
    int waveBits() const {
        return sidstation::sid::waveformBits(paramInt(pp.waveTri), paramInt(pp.waveSaw),
                                             paramInt(pp.wavePulse), paramInt(pp.waveNoise));
    }
    // Same, for one wavetable step's four toggles (noise exclusive).
    int wtStepWaveBits(int step) const {
        if (step < 0 || step >= kWtSteps) return 0;
        const auto& s = pp.wt[step];
        return sidstation::sid::waveformBits(paramInt(s.tri), paramInt(s.saw),
                                             paramInt(s.pulse), paramInt(s.noise));
    }

    // Cross-instance sync of the shared filter and volume.
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void sharedUpdated() override;
    void setParamValue(const char* id, int value);
    // Recompute the player's pitch offset from coarse + fine + the pitch wheel
    // (scaled by the bend range). Cheap: sets a value the frequency stream reads.
    void updatePitchOffset();
    // Effective 0..1 modulation amount for an LFO: its depth, scaled by the mod
    // wheel when its Mod Wheel toggle is on, and ramped in over its fade-in delay.
    double lfoAmount(const ParamPtrs::Lfo& ids, double nowMs) const;
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
    // 6581 ADSR delay bug, which is what drops notes at high decay/sustain. The
    // envelope generator compares a 15-bit LFSR counter against a period picked from
    // the current phase's rate nibble; if the phase or rate changes while the counter
    // has already passed the new period, the counter must run its whole 32767-count
    // sequence before the envelope moves. 32767 / 985248 Hz = up to ~33 ms at PAL.
    // A long decay or release parks the counter at large values, so the failure tracks
    // decay and sustain rather than release.
    static constexpr double kAdsrWrapMs = 33.3;
    // Drain window before a re-attack: the worst-case wrap plus the ~6 ms the fastest
    // release itself takes, so by gate-on the envelope really is at zero AND the
    // counter is cycling on a short period. The old 16 ms was under the wrap alone,
    // which is why the muting was intermittent - the counter's phase at gate-off is
    // effectively random, so it sometimes drained in time and sometimes did not.
    static constexpr double kHardRestartMs = kAdsrWrapMs + 8.0;
    // Gap between the fast-release write and the control re-write that commits it on
    // the one-message-late unit.
    static constexpr double kHardRestartFlushMs = 5.0;
    // Below this much room ahead of the note there is no point draining: the two frames
    // would land on top of the note-on rather than clearing the way for it.
    static constexpr double kHardRestartMinMs = 12.0;
    double voiceClockMs = 0.0;    // target time of the last frame sent, keeps order
    double lastGateOffMs = -1.0e18;  // when this voice last released, to time hard restarts
    int lastPlaying = 0;          // transport state last block, to spot a start
    int lastModPlaying = 0;       // transport state last block in updateModulation, to spot a stop
    double lastPlayheadMs = 0.0;  // playhead last block, to spot a jump
    int lastReleaseGen = 0;       // shared watchdog release generation seen, to clear a stale note
    juce::String currentPresetName;  // last saved/loaded preset, for the editor's display
    bool tooltipsOn = true;          // editor "Tips" toggle, persisted in plugin state
    // A voice-sound parameter (in a preset, and reset by Init): not a shared global,
    // not the voice selection, not the tempo.
    static bool isVoiceSoundParam(const juce::String& id) {
        return !AsidShared::isShared(id) && id != "asidVoice" && id != "bpm";
    }

    sidstation::Lfo pitchStream, pwStream, cutStream;  // one per modulation target
    sidstation::WaveTablePlayer wtPlayer;
    double modTickMs = 0.0;       // one modulation clock for the whole voice
    int wtArp = 0;                // current wavetable arpeggio offset (semitones)
    bool wtOwnsWave = false;      // wavetable is driving the waveform register
    bool lastWtOn = false;        // wtOn last block, to catch the switch-off edge
    double glidePitch = -1.0;     // current sounding pitch (fractional note); -1 = no note
    // Release tail. The gate is low but the envelope is still fading, so the frequency
    // stream has to keep running or the tail freezes at whatever pitch the vibrato last
    // wrote - audibly detuned. Holds the pitch that was sounding and when the fade ends.
    double releaseTailNote = -1.0;
    double releaseTailUntilMs = -1.0e18;
    // Envelope parking, the other half of the ADSR-bug defence. Once the fade has
    // finished, the release nibble is forced to 0: inaudible, since the envelope is
    // already at zero, but it leaves the counter cycling on a SHORT period, so the next
    // attack needs no wrap however long the gap. Where the pre-roll drain needs room
    // ahead of a note, this uses the time after one, of which there is plenty.
    //
    // Emitted from the per-block path rather than scheduled ahead: a long release would
    // put the frame up to 24 s into the queue, where a note starting first would have
    // its own release yanked to 0 mid-flight. parkAtMs is the UNCAPPED fade end, unlike
    // releaseTailUntilMs, so a slow fade is never cut short.
    double parkAtMs = 0.0;
    bool parkPending = false;
    // Bounds the tail so a 24-second release nibble cannot stream for 24 seconds on a
    // MIDI port three voices share. Past this the tail freezes, which by then is far
    // enough down the fade to be inaudible on any normal patch.
    static constexpr double kMaxReleaseTailMs = 4000.0;
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
