# SidStation core protocol library

Framework-agnostic C++17 (no JUCE, no audio, no GUI) encoding of the Elektron
SidStation MIDI/SysEx protocol. This is the foundation the VST3/AU/Standalone
plugin builds on. Everything here is unit-tested against the byte sequences
documented in the SidStation Owners Manual (r22b, OS1.1), pages 38-43.

## Build & test

```sh
cd core && make test
```

Requires only a C++17 compiler. (The plugin itself will build with CMake +
JUCE; this Makefile is just for the pure-C++ core.)

## Layout

| File | Purpose |
|------|---------|
| `include/sidstation/SysEx.h` | SysEx framing constants, init sequence, message-type IDs |
| `include/sidstation/DirectProgram.h` | Direct-Program (single-parameter) message encode/decode |
| `include/sidstation/Parameters.{h}` + `src/Parameters.cpp` | The parameter registry — the single source of truth mapping every editable parameter to its DP address, range, and CC |
| `include/sidstation/ControllerMap.h` | MIDI CC message helper |
| `include/sidstation/Patch.{h}` + `src/Patch.cpp` | Patch-dump nibble codec, name handling, clear/skip messages |
| `tests/tests.cpp` | Dependency-free test harness |

## Two address spaces (important)

The SidStation exposes parameters through **two different memory layouts**:

1. **Direct-Program (DP) map** — sparse live-memory addresses used to set one
   parameter at a time (`Osc1` base `0x47`, 21-byte stride; `LFO1` base `0x86`,
   28-byte stride). This is what `Parameters.cpp` encodes and what drives the
   live, automatable editor.
2. **Patch-dump byte layout** — a compact 143+ byte structure used for full
   patch read/write (`OSC1` at index 36, `LFO1` at 99), nibble-encoded on the
   wire. `Patch.cpp` handles the wire framing today.

The first ~0x24 bytes (name, direct controllers, filter, mode) coincide between
the two; oscillators and LFOs diverge.

## Open questions to confirm on real hardware (milestone 2 — MIDI probe)

- **Patch-dump size field** semantics (logical vs. wire byte count) and whether
  the 10-byte name region is itself nibble-split. The round-trip codec is
  internally consistent regardless, but the on-wire framing needs a real dump to
  confirm.
- **Typed patch-dump field access.** `Patch` currently exposes the name + raw
  bytes with a validated round-trip. Mapping each patch-dump byte to a typed
  field (a second table, distinct from the DP map) is the next increment.
- **Table data** (arpeggiator/waveform sequences, DP positions `0x176`+) is not
  yet modelled.
- **Pitch Sync Speed** is documented as 50..200 but its DP field is 7-bit; the
  upper half is only reachable via a full patch dump. Capped at 127 for DP.
- **Voice-play gate behaviour**: whether setting an oscillator's fixed note via
  DP retriggers the SID envelope/gate — central to the three-voice play engine.
