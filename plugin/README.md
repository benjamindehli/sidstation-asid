# SidStation Editor plugin

VST3, AU and Standalone editor for the Elektron SidStation, built with JUCE on top of the framework agnostic `core` protocol library.

## What the plugin does

It builds as VST3, AU and Standalone on macOS.

It presents to the DAW as an instrument, MIDI in and audio out.

It generates an automatable parameter for every entry in the core parameter registry, around 130 of them.

On any parameter change, from the UI or from DAW automation, it emits the matching Direct Program SysEx to the SidStation over a MIDI device it opens itself.

The editor has two tabs. Parameters is a custom, scrollable view with knobs for numeric values, toggles for switches, and dropdowns for the named choices like waveform, LFO type and filter mode. Librarian opens the MIDI device, lists the patches in a folder as individual entries, and sends, receives and saves patches as .syx files.

Restoring a saved project does not push parameters at the unit. Direct Program emission is suppressed during state restore, so opening a project does not overwrite the unit patch with defaults.

Still to come, the three voice play engine, audio pass through from the hardware input, and custom visual design.

## Build

From the repo root:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

JUCE is downloaded automatically via CMake FetchContent, pinned in `plugin/CMakeLists.txt`. Bump `GIT_TAG` to update. The first configure clones JUCE and takes a while.

Artifacts land under `build/plugin/SidStationEditor_artefacts/`. With `COPY_PLUGIN_AFTER_BUILD` the AU and VST3 are also copied into your user plugin folders. The Standalone app is the easiest way to test.

## MIDI setup

The plugin opens the USB MIDI interface directly, chosen in the Librarian tab, rather than relying on DAW routing. Connect the interface DIN OUT to the SidStation MIDI IN to send, and the SidStation MIDI OUT to the interface IN to receive dumps.

Bulk transfers (a bank or a full parameter push) are paced automatically, since the SidStation drops SysEx that arrives too fast. Instruments used as interfaces tend to filter SysEx, so a dedicated class compliant USB MIDI interface is the reliable choice.

Sending a single patch from the Librarian overwrites only the currently selected patch on the unit. Restoring a whole bank file is destructive and wipes patch memory first, so that is left as a deliberate power user action.
