// SidStation "Direct Program" (DP) SysEx messages - set a single parameter in
// the unit's live memory without sending a whole patch dump.
//
// Wire format (manual pages 39-42):
//   F0 00 20 3C 01 00 04   <- init + DirectProgram message type
//   %00000aaa              <- memory position, high 3 bits
//   %0aaaaaaa              <- memory position, low 7 bits
//   %0mmmmmmm              <- field mask
//   %00000sss              <- left-shift amount for the field
//   %0ddddddd              <- field data (raw value, pre-shift)
//   F7
//
// The unit applies the write as:  byte = (byte & ~(mask<<shift)) | ((data &
// mask) << shift)
#pragma once

#include <cstdint>

#include "SysEx.h"

namespace sidstation {

// Addresses a bit-field within one byte of the SidStation's DP memory map.
struct DpAddress {
  std::uint16_t position = 0; // 10-bit memory position
  Byte mask = 0x7F;           // field width mask (applied before shift)
  Byte shift = 0;             // left shift into the target byte

  constexpr DpAddress() = default;
  constexpr DpAddress(std::uint16_t pos, Byte m, Byte s)
      : position(pos), mask(m), shift(s) {}
};

// Encodes a Direct Program message. `data` is the raw field value (0..mask).
// It is NOT pre-shifted - the unit shifts it internally.
inline Bytes encodeDirectProgram(const DpAddress &addr, Byte data) {
  Bytes out;
  out.reserve(13);
  sysex::appendInit(out);
  out.push_back(static_cast<Byte>(sysex::MessageType::DirectProgram));
  out.push_back(static_cast<Byte>((addr.position >> 7) & 0x07)); // high 3 bits
  out.push_back(static_cast<Byte>(addr.position & 0x7F));        // low 7 bits
  out.push_back(static_cast<Byte>(addr.mask & 0x7F));
  out.push_back(static_cast<Byte>(addr.shift & 0x7F));
  out.push_back(static_cast<Byte>(data & addr.mask));
  out.push_back(sysex::kEnd);
  return out;
}

// Parsed view of a Direct Program message.
struct DecodedDirectProgram {
  bool valid = false;
  DpAddress address;
  Byte data = 0;
};

// Parses a DP message. Returns {valid=false} on any framing/length mismatch.
inline DecodedDirectProgram decodeDirectProgram(const Bytes &msg) {
  DecodedDirectProgram r;
  // init(6) + type(1) + posHi + posLo + mask + shift + data + F7 = 13 bytes.
  if (msg.size() != 13)
    return r;
  if (!sysex::hasValidInit(msg))
    return r;
  if (msg[6] != static_cast<Byte>(sysex::MessageType::DirectProgram))
    return r;
  if (msg.back() != sysex::kEnd)
    return r;
  r.address.position = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(msg[7] & 0x07) << 7) | (msg[8] & 0x7F));
  r.address.mask = msg[9];
  r.address.shift = msg[10];
  r.data = msg[11];
  r.valid = true;
  return r;
}

} // namespace sidstation
