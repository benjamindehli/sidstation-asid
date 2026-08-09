# Changelog

All notable changes to SidStation ASID are recorded here. This project follows [semantic versioning](https://semver.org) and the format of [Keep a Changelog](https://keepachangelog.com). The full notes for each release are in [release-notes/](release-notes/).

## [1.1.0] - 2026-08-09

### Added

- Windows setup installer (Inno Setup) that installs the VST3 and Standalone.
- Linux tarball with the VST3 and Standalone plus install notes, for x86_64.

### Notes

- The Windows and Linux builds compile and package but are not yet hardware-tested. The macOS build is unchanged.

## [1.0.0] - 2026-08-09

First stable release: a cross platform plugin (VST3, AU, Standalone) that plays the three SID voices of the Elektron SidStation directly over the ASID protocol, one voice per instance.

### Added

- Per voice: the four combinable SID waveforms (noise exclusive as on the real chip), an ADSR envelope, pulse width, coarse and fine tuning, pitch bend, hard sync, ring modulation, the raw TEST bit, portamento, an eight step wavetable, and three LFOs on pitch, pulse width and cutoff.
- Shared across the three voices: the filter (cutoff, resonance, per voice routing, external input, and combinable low, band and high pass modes), master volume, and a switch to silence voice 3.
- A Commodore 64 styled pixel interface with per voice colour coding, voice sound presets, an Init and a Panic, a tempo readout, a MIDI load meter, and hover hints that can be toggled from the preset bar.
- A signed and notarized universal macOS installer (a .pkg inside a .dmg).

[1.1.0]: https://github.com/benjamindehli/sidstation-asid/releases/tag/v1.1.0
[1.0.0]: https://github.com/benjamindehli/sidstation-asid/releases/tag/v1.0.0
