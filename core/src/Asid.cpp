#include "sidstation/Asid.h"

#include <cmath>

namespace sidstation {
namespace {

constexpr Byte kAsidManId = 0x2D;
constexpr Byte kCmdStart  = 0x4C;
constexpr Byte kCmdStop   = 0x4D;
constexpr Byte kCmdUpdate = 0x4E;
constexpr Byte kCmdLcd    = 0x4F;

// ASID slot id -> SID register (from the Paulus spec / vap regid.h). The three
// voice control registers (4, 11, 18) sit at ids 21..23... note ids 22/23/24 map
// to 4/11/18, and 25/26/27 are their secondary slots for double writes.
constexpr Byte kSlotToReg[28] = {
    0,  1,  2,  3,  5,  6,  7,   // ids 0..6
    8,  9,  10, 12, 13, 14, 15,  // ids 7..13
    16, 17, 19, 20, 21, 22, 23,  // ids 14..20
    24, 4,  11, 18, 4,  11, 18,  // ids 21..27
};

}  // namespace

int asidSlotForRegister(Byte reg) {
    // Primary slot only (ids 0..24). The secondary slots 25..27 are reserved for
    // the gate double-write case and are not used for a plain register write.
    for (int id = 0; id < 25; ++id)
        if (kSlotToReg[id] == reg) return id;
    return -1;
}

Bytes encodeAsidStart() { return {sysex::kStart, kAsidManId, kCmdStart, sysex::kEnd}; }
Bytes encodeAsidStop()  { return {sysex::kStart, kAsidManId, kCmdStop, sysex::kEnd}; }

Bytes encodeAsidLcd(const std::string& text) {
    Bytes out{sysex::kStart, kAsidManId, kCmdLcd};
    for (char c : text) out.push_back(static_cast<Byte>(c) & 0x7F);
    out.push_back(sysex::kEnd);
    return out;
}

Bytes encodeAsidUpdate(const std::vector<SidWrite>& writes) {
    // Collect the value per slot (last write to a register wins).
    Byte value[28] = {0};
    bool present[28] = {false};
    for (const auto& w : writes) {
        const int id = asidSlotForRegister(w.reg);
        if (id < 0) continue;
        present[id] = true;
        value[id] = w.value;
    }

    Byte mask[4] = {0}, msb[4] = {0};
    Bytes data;
    for (int id = 0; id < 28; ++id) {
        if (!present[id]) continue;
        const int byteIndex = id / 7;
        const int bit = id % 7;
        mask[byteIndex] = static_cast<Byte>(mask[byteIndex] | (1 << bit));
        if (value[id] & 0x80) msb[byteIndex] = static_cast<Byte>(msb[byteIndex] | (1 << bit));
        data.push_back(static_cast<Byte>(value[id] & 0x7F));
    }

    Bytes out{sysex::kStart, kAsidManId, kCmdUpdate};
    for (int i = 0; i < 4; ++i) out.push_back(mask[i]);
    for (int i = 0; i < 4; ++i) out.push_back(msb[i]);
    out.insert(out.end(), data.begin(), data.end());
    out.push_back(sysex::kEnd);
    return out;
}

Bytes SidState::fullUpdate() const {
    std::vector<SidWrite> writes;
    writes.reserve(25);
    for (Byte r = 0; r <= 0x18; ++r) writes.push_back({r, reg[r]});
    return encodeAsidUpdate(writes);
}

std::uint16_t sidFrequency(double midiNote, double clockHz) {
    const double hz = 440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0);
    double fn = hz * 16777216.0 / clockHz;
    if (fn < 0.0) fn = 0.0;
    if (fn > 65535.0) fn = 65535.0;
    return static_cast<std::uint16_t>(std::lround(fn));
}

}  // namespace sidstation
