# Changelog

All notable changes to SidStation ASID are recorded here. This project follows [semantic versioning](https://semver.org) and the format of [Keep a Changelog](https://keepachangelog.com). The full notes for each release are in [release-notes/](release-notes/).

## [1.1.2] - 2026-08-11

No changes to the plugin. The macOS package failed to build in 1.1.1, so that version published without a DMG and this one carries the same fixes to macOS users.

### Fixed

- The macOS release job ran out of disk while building the DMG. The universal build tree is now removed once the installer package exists, since nothing after that step reads it.

## [1.1.1] - 2026-08-11

### Fixed

- Notes dropping out on fast passages with a high decay, a 6581 envelope rate counter problem now handled by sending decay 0 at full sustain, draining the envelope before a fresh attack that lands on a releasing note, and parking the envelope once its fade has finished.
- Muted notes on legato and quick re-attacks: a note landing on an oscillator that is already sounding retunes instead of retriggering.
- Pitch modulation stopping during the envelope's release period.
- Tempo synced LFOs drifting, by counting cycle boundaries from the song position instead of inferring them from the phase.
- The editor zeroing the decay parameter at full sustain instead of leaving the value and showing the control as inactive.
- Preset names are sanitised before being used as filenames.
- MIDI frames queued while no output port is open are dropped rather than flushed when one opens.
- A shutdown crash in some hosts caused by the look and feel being destroyed while still in use.
- A race in the state shared between plugin instances.

### Changed

- Parameter lookups are cached off the audio thread.
- Unused MIDI receive and bulk send paths removed from the device layer.

### Notes

- No parameters were added or removed, so presets and automation from 1.1.0 carry over unchanged.
- A high decay with sustain below 15 can still drop notes on fast parts. That is a limit of the chip, and keeping decay lower is the workaround.

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

[1.1.2]: https://github.com/benjamindehli/sidstation-asid/releases/tag/v1.1.2
[1.1.1]: https://github.com/benjamindehli/sidstation-asid/releases/tag/v1.1.1
[1.1.0]: https://github.com/benjamindehli/sidstation-asid/releases/tag/v1.1.0
[1.0.0]: https://github.com/benjamindehli/sidstation-asid/releases/tag/v1.0.0
