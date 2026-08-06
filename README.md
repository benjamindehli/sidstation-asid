# SidStation plugins

Two cross platform plugins (VST3, AU, Standalone) for the Elektron SidStation, the MOS6581 SID desktop synth.

macOS first.

> **Under development.** This is a work in progress, not a finished release. Things are still changing, features are incomplete, and you should expect rough edges and breaking changes. Use it to experiment, not in anything you rely on.

## The two plugins

SidStation Editor manages and edits patches. It exposes the patch parameters as automatable controls and sends changes to the unit as MIDI Control Change. It also has a patch librarian that reads patches off the unit and stores them as .syx files. This is the "manage my sounds" tool.

SidStation ASID plays the three SID voices directly. It drives the raw SID chip over the ASID protocol, so it gives independent control of each voice with its own pitch and gate, plus the things CC cannot reach. One instance drives one voice, so in a DAW you put an instance on each of three tracks and pick a voice per track. This is the "play the three voices" tool that the SidStation makes awkward on its own.

They share one code base. The `core` library holds the protocol, and `common` holds the MIDI device layer.

## What SidStation ASID does today

Per voice:

- The four SID waveforms, combinable, with noise exclusive as on the real chip.
- ADSR envelope, pulse width, coarse and fine tuning, and pitch bend with an adjustable range.
- Hard sync, ring modulation, and the raw TEST bit.
- Portamento, with a legato or always trigger and a smooth or stepped glide.
- An eight step wavetable, each step with its own waveform, sync, ring, pulse width and arpeggio offset, plus speed, length and a loop point.
- Three LFOs (pitch, pulse width, cutoff) with several shapes, a free Hz or host tempo synced rate, depth, mod wheel scaling, and a fade in delay.

Shared across the three voices, since the chip has one of each:

- The filter: cutoff, resonance, per voice routing, external input, and combinable low, band and high pass modes.
- Master volume, and a switch to silence voice 3 so it can serve as a pure modulation source for ring and sync.

Around the sound:

- Per voice colour coding, and one instance per voice so two windows cannot fight over the same voice.
- A tempo readout (the host tempo in a DAW, or set by hand in Standalone), a Panic that releases every voice, and an Init that resets a voice to its default sound.
- A MIDI load meter, since the SidStation shares one slow MIDI port across all three voices.

## Status

- SidStation ASID is the furthest along and is usable for playing the three voices.
- SidStation Editor (patch editing over CC, and the librarian) is earlier and less exercised.
- Presets, so a voice patch can be saved and recalled outside a DAW project, are not in yet.
- Developed and tested against OS 1.11 R34 hardware on macOS. Windows and Linux are not exercised.

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
