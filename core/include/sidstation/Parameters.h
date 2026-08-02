// SidStation parameter registry - the single source of truth that ties each
// editable parameter to its Direct-Program address, its logical value range,
// and (where one exists) its MIDI CC number.
//
// This table drives both the plugin's automatable parameter tree and the
// message encoders. It is built from the DP map (manual pages 41-42), the
// patch-data bit layouts (page 41), and the CC assignments (pages 38-39).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "DirectProgram.h"

namespace sidstation {

enum class ParamKind {
    Continuous,  // 0..max linear value
    Bipolar,     // signed, stored as 7-bit two's complement (transpose, detune)
    Bool,        // 0/1 switch
    Enum,        // discrete choice (waveform, LFO type, CTRL source, ...)
};

// A named value for an Enum parameter. Values can be non-contiguous (e.g. the
// oscillator waveform: 1=Triangle, 2=Saw, 4=Pulse, 5=Mixed, 8=Noise), so both
// the device value and its label are stored.
struct EnumChoice {
    int         value;
    std::string label;
};

struct ParamInfo {
    std::string id;     // stable key, e.g. "osc1.attack"
    std::string name;   // human label, e.g. "Osc 1 Attack"
    std::string group;  // UI grouping, e.g. "Osc 1"
    DpAddress   dp;     // Direct-Program address (position/mask/shift)
    int         minValue = 0;
    int         maxValue = 127;
    ParamKind   kind     = ParamKind::Continuous;
    int         cc       = -1;  // MIDI CC number, or -1 if none
    std::vector<EnumChoice> choices;  // named values, for Enum params that have them
};

// The full parameter table. Built once, returned by const reference.
const std::vector<ParamInfo>& parameters();

// Lookups (return nullptr / nullopt when not found).
const ParamInfo* findParamById(const std::string& id);
const ParamInfo* findParamByCc(int cc);

// Converts a logical parameter value into the DP `data` byte (handles Bipolar
// two's-complement encoding and clamps to the parameter's range/mask).
Byte encodeParamValue(const ParamInfo& p, int value);

// Inverse of encodeParamValue: DP data byte -> logical value.
int decodeParamValue(const ParamInfo& p, Byte data);

// Builds the Direct-Program SysEx message that sets `p` to `value`.
inline Bytes directProgramFor(const ParamInfo& p, int value) {
    return encodeDirectProgram(p.dp, encodeParamValue(p, value));
}

}  // namespace sidstation
