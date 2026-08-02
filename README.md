# SidStation Editor

A cross platform editor plugin (VST3, AU, Standalone) for the Elektron SidStation, the MOS6581 SID desktop synth. It exposes all patch parameters as automatable DAW parameters, manages and edits patches, and aims to make the three voices easy to play. macOS comes first.

## Layout

The `core` directory holds a framework agnostic C++17 library that encodes the SidStation MIDI and SysEx protocol. It has no dependencies and is unit tested.

The `probe` directory holds a small command line tool that validates the protocol against real hardware. It uses CoreMIDI on macOS and a dry run backend elsewhere.

The `plugin` directory holds the JUCE plugin (VST3, AU, Standalone) built on top of `core`.

## Build

To build the plugin and core together with CMake and JUCE, run:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

JUCE is fetched automatically. See `plugin/README.md` for details and artifact locations.

To run the core library tests without JUCE:

```sh
cd core && make test
```

To build the hardware probe:

```sh
cd probe && ./build.sh && ./sidprobe
```

## Status

The core protocol library is done and unit tested. It covers the parameter registry, the Direct Program messages, and the patch dump codec.

The standalone MIDI probe is done. The SysEx path is validated end to end through a virtual MIDI loopback.

The plugin builds and runs as VST3, AU and Standalone. Every parameter is automatable and edits are sent to the unit as Direct Program messages. The editor has a custom parameter view with knobs, toggles and named dropdowns, plus a patch librarian for saving, loading and sending patches as .syx files.

Still to come, the three voice play engine, audio pass through from the hardware, and a notarized build for distribution.

See the per directory README files for specifics, and `core/README.md` for the protocol notes and the open questions that still need hardware to confirm.
