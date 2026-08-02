// MIDI Control Change helpers for the SidStation.
//
// On this unit's firmware (OS 1.11 R34) Direct Program is dead, so CC is the
// working live edit path. The SidStation receives around 100 CC assignments
// (manual pages 38-39), each carried on ParamInfo::cc.
#pragma once

#include <cmath>

#include "Parameters.h"
#include "SysEx.h"

namespace sidstation {

// Maps a parameter's logical value onto a 0..127 CC value. Booleans send 0 or
// 127. Everything else linearly maps the parameter's own range onto 0..127, and
// the unit scales that back to the parameter's resolution. This matches the
// verified case (filter cutoff 0..127 passes through unchanged). The sub-range
// and bipolar scaling is a hardware calibration point still to confirm.
inline int ccValue(const ParamInfo& p, int logicalValue) {
    int v = logicalValue < p.minValue ? p.minValue
                                      : (logicalValue > p.maxValue ? p.maxValue : logicalValue);
    if (p.kind == ParamKind::Bool) return v != 0 ? 127 : 0;
    if (p.maxValue <= p.minValue) return 0;
    long scaled = std::lround(static_cast<double>(v - p.minValue) /
                              static_cast<double>(p.maxValue - p.minValue) * 127.0);
    return static_cast<int>(scaled < 0 ? 0 : (scaled > 127 ? 127 : scaled));
}

// Builds a 3-byte CC message (Bn cc value) on `channel` (0..15) for `p`, or an
// empty vector if the parameter has no CC assignment.
inline Bytes controlChange(int channel, const ParamInfo& p, int logicalValue) {
    if (p.cc < 0) return {};
    return {static_cast<Byte>(0xB0 | (channel & 0x0F)),
            static_cast<Byte>(p.cc & 0x7F),
            static_cast<Byte>(ccValue(p, logicalValue))};
}

}  // namespace sidstation
