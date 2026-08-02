// MIDI Control Change helpers for the SidStation.
//
// The SidStation receives roughly 100 CC assignments (manual pages 38-39), all
// carried on ParamInfo::cc. Direct-Program messages cover every parameter and
// are the plugin's primary edit path. CC is offered as an alternative for the
// subset the unit exposes as controllers.
#pragma once

#include "Parameters.h"
#include "SysEx.h"

namespace sidstation {

// Builds a 3-byte CC message (Bn cc value) on `channel` (0..15) for `p`.
// The value is clamped to the parameter's range and 0..127. Returns an empty
// vector if the parameter has no CC assignment.
inline Bytes controlChange(int channel, const ParamInfo& p, int value) {
    if (p.cc < 0) return {};
    int v = value < p.minValue ? p.minValue : (value > p.maxValue ? p.maxValue : value);
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    return {static_cast<Byte>(0xB0 | (channel & 0x0F)),
            static_cast<Byte>(p.cc & 0x7F),
            static_cast<Byte>(v & 0x7F)};
}

}  // namespace sidstation
