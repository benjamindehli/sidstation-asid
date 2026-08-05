// Three voice play engine (milestone 5).
//
// The SidStation does not natively let you play its three oscillators as three
// separate monophonic voices from the keyboard. You normally have to set each
// oscillator to a fixed note by entering a pitch value. This engine removes
// that step. It takes incoming MIDI note events and works out, per oscillator,
// what pitch it should play and whether it should sound, so the plugin can then
// send the matching Direct Program messages.
//
// Framework agnostic and unit tested. The engine only decides the musical
// intent (which oscillator plays which note). Turning that into actual MIDI or
// DP bytes, and the exact gate behaviour, is done by the caller and calibrated
// against real hardware.
#pragma once

#include <array>
#include <vector>

namespace sidstation {

// Maps a MIDI note (0..127) to a SidStation oscillator fixed note value
// (OSC_TRACK, 1..99). The offset is a hardware calibration point, the default
// is a first guess to confirm against the unit.
int sidNoteFromMidi(int midiNote, int offset = 12);

enum class VoiceMode {
    PerChannel,  // MIDI channel 1, 2, 3 drive oscillator 1, 2, 3, each monophonic
};

// One instruction for one oscillator.
struct VoiceAction {
    int  oscillator = 0;     // 0..2
    bool gateOn = false;     // true to sound the note, false to release it
    bool retrigger = true;   // gate-on only: false = true legato (retune, keep the
                             // envelope running); true = re-attack the envelope
    int  sidNote = 0;        // OSC_TRACK fixed note value, valid when gateOn
    int  midiNote = -1;      // the source MIDI note
    int  velocity = 0;       // the source velocity, when gateOn
};

class VoiceEngine {
public:
    void setMode(VoiceMode m) { mode = m; }
    VoiceMode getMode() const { return mode; }

    void setNoteOffset(int o) { offset = o; }
    int noteOffset() const { return offset; }

    // Feed MIDI note events (channel is 0 based). Each call returns the actions
    // to apply, usually zero or one.
    std::vector<VoiceAction> noteOn(int channel, int midiNote, int velocity);
    std::vector<VoiceAction> noteOff(int channel, int midiNote);

    // Releases every held note across all oscillators.
    std::vector<VoiceAction> allNotesOff();

    // Clears all state without emitting actions.
    void reset();

private:
    int oscForChannel(int channel) const;

    VoiceMode mode = VoiceMode::PerChannel;
    int offset = 12;

    // Per oscillator stack of held notes, newest at the back, for last note
    // priority with legato fall back to the previously held note.
    std::array<std::vector<int>, 3> held;
};

}  // namespace sidstation
