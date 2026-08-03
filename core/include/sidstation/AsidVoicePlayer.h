// Minimal three-voice ASID play engine.
//
// Turns MIDI notes on channels 1, 2, 3 into direct SID register updates for
// voices 1, 2, 3, using ASID. Each voice gets its own frequency (accurate, from
// the note) and its own gate, which is the clean way to play the three voices
// that was the whole point. The SID chip runs the envelopes, so we only emit
// register writes when something changes.
//
// This is the "minimal playable" version: a sensible default sound (sawtooth,
// full sustain, voices unfiltered, master volume up) plus a few global controls.
// Richer per-voice editing comes later. Framework agnostic and tested.
#pragma once

#include <vector>

#include "Asid.h"
#include "VoiceEngine.h"

namespace sidstation {

class AsidVoicePlayer {
public:
    AsidVoicePlayer() { reset(); }

    // Restores the default SID state (does not emit anything).
    void reset();

    // Messages to send when entering ASID mode (start command + full state) and
    // when leaving (gate everything off, then stop).
    std::vector<Bytes> start();
    std::vector<Bytes> stop();

    // Note events return the ASID frames to send. Each state change is sent
    // twice back to back: the unit appears to apply a write only when the next
    // message arrives, so the second frame flushes the first into effect.
    std::vector<Bytes> noteOn(int channel, int midiNote, int velocity);
    std::vector<Bytes> noteOff(int channel, int midiNote);

    // Live controls. Each returns the single ASID update for the change (the
    // caller sends it twice to flush it into effect, like note frames).
    Bytes setVolume(int vol0to15);
    Bytes setCutoff(int cutoff0to2047);
    Bytes setResonance(int res0to15);
    Bytes setFilterMode(Byte modeBits);              // sid::kLowPass / kBandPass / kHighPass
    Bytes setWaveform(int voice, Byte waveBits);     // sid::kTriangle / kSaw / kPulse / kNoise
    Bytes setPulseWidth(int voice, int pw0to4095);
    Bytes setAttackDecay(int voice, int attack0to15, int decay0to15);
    Bytes setSustainRelease(int voice, int sustain0to15, int release0to15);
    Bytes setFilterRouting(int voice, bool routeThroughFilter);
    // Writes the whole resonance and routing register (0x17) at once. The
    // routing bits are shared by all voices, so the caller passes the full set.
    Bytes setResonanceRouting(int res0to15, int routingBits0to7);
    Bytes setSync(int voice, bool on);   // hard sync to the next voice's oscillator
    Bytes setRing(int voice, bool on);   // ring modulation with the next voice

    // Rewrites the frequency register at the current note offset by `semitones`
    // (for pitch modulation). Returns empty when the voice has no note sounding.
    Bytes setPitchMod(int voice, double semitones);

    void setClock(double hz) { clockHz = hz; }

    // Which SID voice this player drives. -1 means the three-voice, per-channel
    // mode (channel 1/2/3 to voice 1/2/3). 0/1/2 means every note goes to that
    // one voice, which is how a per-track plugin instance is used.
    void setTargetVoice(int v) { targetVoice = v; }
    int targetVoice_() const { return targetVoice; }

    const SidState& state() const { return sidState; }

private:
    // Applies one voice action to the state and returns the single ASID frame.
    Bytes frameForAction(const VoiceAction& a);
    // Runs the actions and returns each resulting frame twice (frame + flush).
    std::vector<Bytes> framesWithFlush(const std::vector<VoiceAction>& actions);

    SidState sidState;
    VoiceEngine alloc;
    double clockHz = sid::kClockPal;
    Byte filterRouting = 0;  // no voices routed through the filter by default
    int targetVoice = -1;
    int currentNote[3] = {-1, -1, -1};  // sounding MIDI note per voice, for pitch mod
};

}  // namespace sidstation
