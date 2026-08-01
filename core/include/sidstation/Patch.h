// SidStation patch-dump SysEx codec (manual pages 39-41).
//
// Patch-dump wire layout:
//   F0 00 20 3C 01 00   init
//   02                  Patch Dump message type
//   00                  dump version
//   %000000aa %0aaaaaaa  9-bit patch-data byte count
//   2D * 24             padding
//   45                  "start of patch data" magic
//   <patch data>        10 ASCII name bytes, then nibble-encoded data bytes
//   F7                  end
//
// "Patch data" bytes 0..9 are the patch name in ASCII. Every byte after the
// name is transmitted as two MIDI bytes: high nibble then low nibble.
//
// NOTE: the exact meaning of the size field and whether the name region is
// itself nibble-split are the two points to confirm against real hardware in
// the standalone MIDI probe (milestone 2). Round-trip encode/decode here is
// internally consistent regardless.
#pragma once

#include <optional>
#include <string>

#include "SysEx.h"

namespace sidstation {

struct Patch {
    static constexpr std::size_t kNameLength = 10;

    // Full logical patch-data byte array (index 0.. ). Bytes 0..9 hold the
    // ASCII name; the remainder follow the patch-data layout on manual page 41.
    Bytes data;

    std::string name() const;
    void setName(const std::string& n);  // padded/truncated to 10 chars
};

// Builds a complete Patch Dump SysEx message from `patch`.
Bytes encodePatchDump(const Patch& patch);

// Parses a Patch Dump SysEx message. Returns nullopt on framing mismatch.
std::optional<Patch> decodePatchDump(const Bytes& msg);

// "Patch all clear" — wipes every patch position (manual page 39).
Bytes encodePatchAllClear();

// "Skip Patch" — advances the current patch position (manual page 39).
Bytes encodeSkipPatch();

}  // namespace sidstation
