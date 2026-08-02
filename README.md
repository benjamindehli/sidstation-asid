# SidStation plugins

Two cross platform plugins (VST3, AU, Standalone) for the Elektron SidStation, the MOS6581 SID desktop synth.

macOS first.

## The two plugins

SidStation Editor manages and edits patches. It exposes the patch parameters as automatable controls and sends changes to the unit as MIDI Control Change. It also has a patch librarian that reads patches off the unit and stores them as .syx files. This is the "manage my sounds" tool.

SidStation ASID plays the three SID voices directly. It drives the raw SID chip over the ASID protocol, so it gives independent control of each voice with its own pitch and gate, plus the things CC cannot reach. One instance drives one voice, so in a DAW you put an instance on each of three tracks and pick a voice per track. This is the "play the three voices" tool that the SidStation makes awkward on its own.

They share one code base. The `core` library holds the protocol, and `common` holds the MIDI device layer.

## Layout

| Directory | What it is                                                                                                       |
| --------- | ---------------------------------------------------------------------------------------------------------------- |
| `core/`   | Framework agnostic C++17 library for the SidStation MIDI, SysEx and ASID protocols. No dependencies. Unit tested.|
| `common/` | The shared MIDI device layer (MidiHub), used by both plugins.                                                    |
| `plugin/` | SidStation Editor (patch editing over CC, and the librarian).                                                    |
| `asid/`   | SidStation ASID (three voice play).                                                                              |
| `probe/`  | A standalone CLI for validating the protocol against real hardware.                                              |

## Build

Both plugins, via CMake and JUCE:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

JUCE is fetched automatically by the root CMakeLists and shared by both plugins. With copy after build on, the AU and VST3 land in your user plugin folders, and the two Standalone apps are the quickest way to test.

To run the core library tests without JUCE:

```sh
cd core && make test
```

## Hardware notes

Most of the hard lessons about talking to a real unit are recorded in `core/README.md`, including the important one: on the OS 1.11 R34 firmware, the SysEx Direct Program path is dead, so the editor drives the unit over CC, and ASID is how the three voice play works.
