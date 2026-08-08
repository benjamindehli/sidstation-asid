// ASID protocol, direct SID register streaming over MIDI SysEx.
//
// On the user's firmware (OS 1.11 R34) Direct Program is dead, but ASID mode
// works. ASID bypasses the SidStation patch engine and writes the raw SID chip
// registers, which gives direct control of everything the chip can do,
// including the parameters CC cannot reach (waveform, resonance, filter mode),
// and true independent per-voice play, since each SID voice has its own
// frequency and gate.
//
// Frame (confirmed against the Jouni Paulus spec and the vap decoder):
//   F0 2D 4E [mask1..4] [msb1..4] [data...] F7
// Four mask bytes (7 bits each) flag which of up to 28 register slots are
// written, four msb bytes carry each register's 8th bit, then one 7-bit data
// byte per flagged slot in ascending slot order.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "SysEx.h"

namespace sidstation {

// SID control-register bit flags (register base+4 of each voice).
namespace sid {
constexpr Byte kGate = 0x01, kSync = 0x02, kRing = 0x04, kTest = 0x08;
constexpr Byte kTriangle = 0x10, kSaw = 0x20, kPulse = 0x40, kNoise = 0x80;
// Filter routing bits (register 0x17 low nibble).
constexpr Byte kFilt1 = 0x01, kFilt2 = 0x02, kFilt3 = 0x04, kFiltExt = 0x08;
// Filter mode bits (register 0x18 high nibble).
constexpr Byte kLowPass = 0x10, kBandPass = 0x20, kHighPass = 0x40, kVoice3Off = 0x80;

// Combine the four waveform toggles into the control-register waveform bits. The
// bits OR together, but on the 6581 noise silences/locks the others, so noise is
// exclusive. Returns 0 (a silent voice) when nothing is selected.
inline Byte waveformBits(bool triangle, bool saw, bool pulse, bool noise) {
    if (noise) return kNoise;
    return static_cast<Byte>((triangle ? kTriangle : 0) | (saw ? kSaw : 0) | (pulse ? kPulse : 0));
}

// PAL 6581 clock, used for the note-to-frequency conversion.
constexpr double kClockPal = 985248.0;
constexpr double kClockNtsc = 1022727.0;
}  // namespace sid

// The 25 writable SID registers (0x00..0x18) plus typed helpers.
struct SidState {
    Byte reg[25] = {0};

    static int voiceBase(int voice) { return voice * 7; }  // voice 0..2 -> 0,7,14

    void setFrequency(int voice, std::uint16_t f) {
        reg[voiceBase(voice) + 0] = static_cast<Byte>(f & 0xFF);
        reg[voiceBase(voice) + 1] = static_cast<Byte>((f >> 8) & 0xFF);
    }
    void setPulseWidth(int voice, std::uint16_t pw12) {  // 12-bit
        reg[voiceBase(voice) + 2] = static_cast<Byte>(pw12 & 0xFF);
        reg[voiceBase(voice) + 3] = static_cast<Byte>((pw12 >> 8) & 0x0F);
    }
    void setControl(int voice, Byte control) { reg[voiceBase(voice) + 4] = control; }
    Byte control(int voice) const { return reg[voiceBase(voice) + 4]; }
    void setGate(int voice, bool on) {
        Byte& c = reg[voiceBase(voice) + 4];
        c = on ? static_cast<Byte>(c | sid::kGate) : static_cast<Byte>(c & ~sid::kGate);
    }
    // Replaces the waveform bits (4..7) while keeping gate/sync/ring/test.
    void setWaveform(int voice, Byte waveBits) {
        Byte& c = reg[voiceBase(voice) + 4];
        c = static_cast<Byte>((c & 0x0F) | (waveBits & 0xF0));
    }
    void setAttackDecay(int voice, Byte attack, Byte decay) {
        reg[voiceBase(voice) + 5] = static_cast<Byte>((attack << 4) | (decay & 0x0F));
    }
    void setSustainRelease(int voice, Byte sustain, Byte release) {
        reg[voiceBase(voice) + 6] = static_cast<Byte>((sustain << 4) | (release & 0x0F));
    }
    void setCutoff(std::uint16_t c11) {  // 11-bit
        reg[21] = static_cast<Byte>(c11 & 0x07);
        reg[22] = static_cast<Byte>((c11 >> 3) & 0xFF);
    }
    void setResonanceRouting(Byte resonance, Byte routingBits) {
        reg[23] = static_cast<Byte>((resonance << 4) | (routingBits & 0x0F));
    }
    void setModeVolume(Byte modeBits, Byte volume) {
        reg[24] = static_cast<Byte>((modeBits & 0xF0) | (volume & 0x0F));
    }

    // ASID update carrying all 25 registers.
    Bytes fullUpdate() const;
};

// One SID register write for a partial ASID update.
struct SidWrite {
    Byte reg;
    Byte value;
};

// Maps a SID register (0x00..0x18) to its primary ASID slot id, or -1 if the
// register is not writable via ASID.
int asidSlotForRegister(Byte reg);

// ASID messages.
Bytes encodeAsidStart();
Bytes encodeAsidStop();
Bytes encodeAsidLcd(const std::string& text);
Bytes encodeAsidUpdate(const std::vector<SidWrite>& writes);

// Builds one update that writes a voice's control register twice, through its
// primary slot (22/23/24) then its secondary slot (25/26/27), which the unit
// applies in order within the single frame. A note-on passes gate low then high
// to retrigger the envelope atomically; a note-off passes gate low twice for a
// robust release. This double-write path is applied reliably by the unit where a
// plain single control write is not. `otherWrites` carries any other registers.
Bytes encodeAsidDoubleControl(int voice, Byte controlFirst, Byte controlSecond,
                              const std::vector<SidWrite>& otherWrites);

// MIDI note (may be fractional for pitch bend) to a 16-bit SID frequency value.
std::uint16_t sidFrequency(double midiNote, double clockHz = sid::kClockPal);

}  // namespace sidstation
