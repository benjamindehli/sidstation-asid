# SidStation Editor

A cross-platform (VST3 / AU / Standalone) editor plugin for the **Elektron
SidStation** — the MOS6581 "SID" desktop synth. It exposes all patch parameters
as automatable DAW parameters, will manage/edit patches, and aims to make the
three voices easy to play. macOS first.

## Layout

| Directory | What it is |
|-----------|-----------|
| `core/`   | Framework-agnostic C++17 library encoding the SidStation MIDI/SysEx protocol. No dependencies. Unit-tested. |
| `probe/`  | Standalone CLI to validate the protocol against real hardware (CoreMIDI on macOS; dry-run elsewhere). |
| `plugin/` | The JUCE plugin (VST3/AU/Standalone) built on `core/`. |

## Build

**Everything (plugin + core), via CMake + JUCE:**
```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
JUCE is fetched automatically. See `plugin/README.md` for details and artifact
locations.

**Core library tests only (no JUCE needed):**
```sh
cd core && make test
```

**The hardware probe:**
```sh
cd probe && ./build.sh && ./sidprobe
```

## Status

- ✅ Core protocol library (parameter registry, Direct-Program + patch codecs), unit-tested.
- ✅ Standalone MIDI probe; SysEx path validated end-to-end via virtual-MIDI loopback.
- ✅ Plugin scaffold: builds VST3/AU/Standalone, all parameters automatable, emits Direct-Program SysEx on change.
- ⏳ Next: custom GUI, patch librarian, audio pass-through, three-voice play engine, notarized distribution.

See per-directory READMEs for specifics, and `core/README.md` for the SidStation
protocol notes and open hardware-validation questions.
