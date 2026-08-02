# SidStation core protocol library

Framework agnostic C++17 (no JUCE, no audio, no GUI) encoding of the Elektron SidStation MIDI and SysEx protocol. This is the foundation the VST3, AU and Standalone plugin builds on. Everything here is unit tested against the byte sequences documented in the SidStation Owners Manual (r22b, OS1.1), pages 38 to 43.

## Build and test

```sh
cd core && make test
```

Requires only a C++17 compiler. The plugin itself builds with CMake and JUCE. This Makefile is just for the pure C++ core.

## Layout

`include/sidstation/SysEx.h` holds the SysEx framing constants, the init sequence, and the message type IDs.

`include/sidstation/DirectProgram.h` holds the Direct Program (single parameter) message encode and decode.

`include/sidstation/Parameters.h` and `src/Parameters.cpp` hold the parameter registry. This is the single source of truth that maps every editable parameter to its Direct Program address, value range, MIDI CC, and named enum choices where it has them.

`include/sidstation/ControllerMap.h` holds the MIDI CC message helper.

`include/sidstation/Patch.h` and `src/Patch.cpp` hold the patch dump nibble codec, name handling, and the clear and skip messages.

`include/sidstation/SysExStream.h` holds the SysEx stream assembler that reassembles complete messages from a raw incoming MIDI byte stream.

`include/sidstation/SyxFile.h` and `src/SyxFile.cpp` hold the .syx file read and write, patch extraction from bulk banks, and folder scanning.

`tests/tests.cpp` holds the dependency free test harness.

## Two address spaces

The SidStation exposes parameters through two different memory layouts.

The Direct Program map uses sparse live memory addresses to set one parameter at a time. Osc 1 sits at base 0x47 with a 21 byte stride, and LFO 1 sits at base 0x86 with a 28 byte stride. This is what `Parameters.cpp` encodes and what drives the live, automatable editor.

The patch dump byte layout is a compact 143 byte plus structure used for full patch read and write. Osc 1 sits at index 36 and LFO 1 at index 99, nibble encoded on the wire. `Patch.cpp` handles the wire framing.

The first 0x24 bytes (name, direct controllers, filter, mode) coincide between the two. Oscillators and LFOs diverge.

## Open questions to confirm on real hardware

Typed patch dump field access. `Patch` currently exposes the name and raw bytes with a validated round trip. Mapping each patch dump byte to a typed field, a second table distinct from the DP map, is the next increment. This is what is needed to load a received patch into the editor controls.

Table data (arpeggiator and waveform sequences, DP positions from 0x176 up) is not yet modelled.

Pitch Sync Speed is documented as 50 to 200 but its DP field is 7 bit, so the upper half is only reachable via a full patch dump. It is capped at 127 for DP.

Voice play gate behaviour. Whether setting an oscillator fixed note via DP retriggers the SID envelope and gate is central to the three voice play engine.

## Findings from real patch banks (2026-08-02)

The codec was validated against four real SidStation .syx banks (Giraya, Klaus P Rausch, Ninjabank, Presets_r1).

Decoding works on real data. Patch names and data decode correctly across roughly 90 to 100 patches per bank. `scanPatchFolder`, `extractPatches` and `extractPatchItems` handle bulk banks.

Bank structure. A bank is one PatchAllClear (0x01) followed by, for each of 128 slots, either a PatchDump (0x02) or a SkipPatch (0x03). Sending a whole bank file is destructive because it wipes patch memory first. Sending a single patch is one 0x02 message and is non destructive.

The size field is unreliable and appears to be ignored on receive. Different banks disagree on its meaning. Giraya uses the byte count, Klaus uses the byte count minus 145. The device parses to F7. Our encoder writes the logical byte count, so a round trip against third party banks differs only in these two bytes and the data is byte identical.

Legacy header. `SidStation_Presets_r1.syx` uses a different, older init (F0 00 45 01 00 instead of F0 00 20 3C 01 00). Our decoder rejects it rather than mis parsing it. Supporting that legacy format is optional.

Bulk sends must be paced. Per Elektron's C6 tool, allow 5 to 50 ms between packets. Zero delay bursts overflow the unit and fail silently. The plugin paces bulk sends with `MidiHub::sendPaced`.
