# SidStation Editor plugin

VST3 / AU / Standalone editor for the Elektron SidStation, built with JUCE on
top of the framework-agnostic `core/` protocol library.

## What the scaffold does (milestone 3)

- Builds as **VST3, AU, and Standalone** on macOS.
- Presents to the DAW as an **instrument** (MIDI in, audio out).
- Generates an automatable parameter for **every** entry in the core parameter
  registry (~130 parameters).
- On any parameter change (from the UI or DAW automation), emits the matching
  **Direct-Program SysEx** into the plugin's MIDI output, which the DAW routes
  to the SidStation. Incoming MIDI (notes, etc.) passes through to the hardware.
- Uses a **generic auto-generated editor** for now.

Not yet (later milestones): custom GUI, audio pass-through from the hardware
input, patch librarian (dump/receive/organize), and the three-voice play engine.
Restoring a saved project does **not** blast parameters at the unit (DP emission
is suppressed during state restore).

## Build

From the repo root:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

JUCE is downloaded automatically via CMake `FetchContent` (pinned in
`plugin/CMakeLists.txt` — bump `GIT_TAG` to update). First configure will take a
while as it clones JUCE.

Artifacts land under `build/plugin/SidStationEditor_artefacts/`. With
`COPY_PLUGIN_AFTER_BUILD`, the AU/VST3 are also copied into your user plugin
folders. The Standalone app is the easiest way to test.

## Testing against hardware

Route the plugin's **MIDI output** to a class-compliant USB-MIDI interface whose
DIN OUT feeds the SidStation's MIDI IN. (Instruments used as interfaces filter
SysEx — see the notes from milestone 2.) Then moving a parameter should send its
Direct-Program message to the unit.
