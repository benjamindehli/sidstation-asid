// SidStation SysEx protocol constants and helpers.
//
// Framework-agnostic. No JUCE, no audio, no GUI dependencies - this header is
// the ground truth for the Elektron SidStation MIDI System-Exclusive format as
// documented in the SidStation Owners Manual (r22b, OS1.1), pages 39-43.
#pragma once

#include <cstdint>
#include <vector>

namespace sidstation {

using Byte = std::uint8_t;
using Bytes = std::vector<Byte>;

// ---------------------------------------------------------------------------
// SysEx framing
// ---------------------------------------------------------------------------
namespace sysex {

constexpr Byte kStart = 0xF0; // SysEx start
constexpr Byte kEnd = 0xF7;   // SysEx end

// The 6-byte "SYSEX init" sequence that prefixes every SidStation SysEx
// message.
//   F0 00 20 3C 01 00
//   |  |  |  |  |  +-- Base channel (padding)
//   |  |  |  |  +----- SidStation ID
//   |  |  |  +-------- Elektron ID
//   |  |  +----------- Europe ID
//   |  +-------------- Europe/USA ID
//   +----------------- SysEx start
constexpr Byte kManufacturerUsaEurope = 0x00;
constexpr Byte kManufacturerEurope = 0x20;
constexpr Byte kElektronId = 0x3C;
constexpr Byte kSidStationId = 0x01;
constexpr Byte kBaseChannelPadding = 0x00;

// Message-type IDs (the byte that immediately follows the init sequence).
enum class MessageType : Byte {
  PatchAllClear = 0x01, // wipe every patch position
  PatchDump = 0x02,     // full patch read/write
  SkipPatch = 0x03,     // advance current patch position
  DirectProgram = 0x04, // set one parameter in live memory
};

// Appends the 6-byte init sequence (F0 00 20 3C 01 00) to `out`.
inline void appendInit(Bytes &out) {
  out.push_back(kStart);
  out.push_back(kManufacturerUsaEurope);
  out.push_back(kManufacturerEurope);
  out.push_back(kElektronId);
  out.push_back(kSidStationId);
  out.push_back(kBaseChannelPadding);
}

// Returns true if `msg` begins with a valid SidStation SysEx init sequence.
inline bool hasValidInit(const Bytes &msg) {
  return msg.size() >= 6 && msg[0] == kStart &&
         msg[1] == kManufacturerUsaEurope && msg[2] == kManufacturerEurope &&
         msg[3] == kElektronId && msg[4] == kSidStationId &&
         msg[5] == kBaseChannelPadding;
}

} // namespace sysex
} // namespace sidstation
