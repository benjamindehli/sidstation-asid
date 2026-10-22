<!-- Generated from docs/index.html by packaging/make-markdown.py. Edit the HTML, then run make markdown. -->

# SidStation ASID

Play the three SID voices of the Elektron SidStation directly, one voice per instance.

Web page: https://benjamindehli.github.io/sidstation-asid/

## What it is

The Elektron SidStation, sometimes written SID Station, is a hardware synth built around the MOS6581 SID chip from the Commodore 64. SidStation ASID streams raw SID registers over the ASID protocol instead of sending MIDI CC, so every register is reachable and each voice gets its own pitch and gate. One instance drives one voice. Put an instance on each of three DAW tracks, pick a voice per track, and the three voices become three monosynths you can sequence apart. Getting that out of the SidStation on its own is awkward.

## What it does

### Per voice

- The four SID waveforms, combinable, with noise exclusive as on the real chip.
- ADSR envelope, pulse width, coarse and fine tuning, and pitch bend with an adjustable range.
- Hard sync, ring modulation, and the raw TEST bit.
- Portamento, with a legato or always trigger and a smooth or stepped glide.
- An eight step wavetable, each step with its own waveform, sync, ring, test, pulse width and arpeggio offset, plus a step length in frames or as a host tempo synced note division, a length and a loop point.
- Three LFOs on pitch, pulse width and cutoff, with several shapes, a free Hz or host tempo synced rate, depth, mod wheel scaling, and a fade in delay.

### Shared across voices

- The filter, since the chip has one: cutoff, resonance, per voice routing, external input, and combinable low, band and high pass modes.
- Master volume, and a switch to silence voice 3 so it can serve as a pure modulation source for ring and sync.

### Everything else

- A Commodore 64 styled pixel interface, colour coded per voice.
- Voice presets saved to disk, so a sound outlives the DAW project it was made in. Init puts a voice back to its default sound.
- A tempo readout, host tempo in a DAW or set by hand in Standalone, and a Panic that releases every voice.
- A MIDI load meter, since the SidStation shares one slow MIDI port across all three voices.

## The four tabs

The dark strip along the top is the same on every tab. It carries the MIDI output picker with a Scan button, the tempo and the clock rate, a MIDI load meter, the voice selector, and the preset bar.

### Voice

Everything that shapes a single voice. Oscillator has the four combinable waveforms with Sync, Ring, Test and Pulse Width. Tuning has coarse and fine pitch, the bend range, the glide time, and the glide trigger and type switches. Amp Envelope is the ADSR.

![The Voice tab, with the oscillator waveforms, tuning and ADSR envelope](https://benjamindehli.github.io/sidstation-asid/assets/screenshots/sidstation-asid-voice-tab_976.webp)

### Wavetable

An eight step table that drives the waveform and more, one step per tick. The top row sets whether it runs, plus speed, length and the loop point. Each step row carries its waveform, Sync, Ring and Test, a pulse width fader, and an arpeggio offset. The loop point and the playing step are marked on the left. What to put in those steps is its own subject, which [sound design](https://benjamindehli.github.io/sidstation-asid/sound-design/) takes up along with the LFOs and the chip rules behind them.

![The Wavetable tab, with eight step rows and the loop point marked](https://benjamindehli.github.io/sidstation-asid/assets/screenshots/sidstation-asid-wavetable-tab_976.webp)

### Modulation

Two LFOs for this voice, one on pitch and one on pulse width. Each has a shape, a rate that is either free in Hz or locked to the host tempo as a note division, a depth, a Mod Wheel switch so the wheel scales the depth, and a fade in Delay.

![The Modulation tab, with the pitch and pulse width LFOs](https://benjamindehli.github.io/sidstation-asid/assets/screenshots/sidstation-asid-modulation-tab_976.webp)

### Global

The settings the SID shares across all three voices. Filter has per voice routing, the external input, the combinable low, band and high pass modes, and Cutoff and Resonance. Cutoff Modulation is a third LFO on the filter cutoff. Master has the Voice 3 Off switch, Volume, Latency and Panic.

![The Global tab, with the shared filter, cutoff modulation and master controls](https://benjamindehli.github.io/sidstation-asid/assets/screenshots/sidstation-asid-global-tab_976.webp)

## Download

Version 1.2.1, free and GPL v3. One file per platform, each carrying every plugin format that platform has. The [releases page](https://github.com/benjamindehli/sidstation-asid/releases/latest) keeps the earlier versions and the source archives, and the [changelog](https://github.com/benjamindehli/sidstation-asid/blob/main/CHANGELOG.md) says what changed in each. The plugin is also listed on [KVR Audio](https://www.kvraudio.com/product/sidstation-asid-by-dehli-musikk).

### macOS

10.15 or later, Apple Silicon and Intel. Signed and notarized, so it opens without a Gatekeeper warning.

Installs the AU, the VST3 and the Standalone. The largest of the three files, since it carries universal builds of all three.

`SidStation-ASID-1.2.1.dmg`

[Download for macOS](https://github.com/benjamindehli/sidstation-asid/releases/download/v1.2.1/SidStation-ASID-1.2.1.dmg)

### Windows

An Inno Setup installer. Unsigned, so SmartScreen may warn: choose More info, then Run anyway.

Installs the VST3 and the Standalone. There is no AU, which is a macOS format.

`SidStation-ASID-1.2.1-setup.exe`

[Download for Windows](https://github.com/benjamindehli/sidstation-asid/releases/download/v1.2.1/SidStation-ASID-1.2.1-setup.exe)

### Linux

An x86_64 tarball, unsigned. Nothing is installed for you: copy the VST3 into place and run the Standalone where it lands.

Carries the VST3 and the Standalone, plus install notes and the licence.

`SidStation-ASID-1.2.1-linux-x86_64.tar.gz`

[Download for Linux](https://github.com/benjamindehli/sidstation-asid/releases/download/v1.2.1/SidStation-ASID-1.2.1-linux-x86_64.tar.gz)

All three need an Elektron SidStation. The plugin contains no SID emulation and makes no sound on its own. Only the macOS build has been tested against real hardware so far. [Install steps](https://benjamindehli.github.io/sidstation-asid/#install) are below.

## Install

The three files are in [download](https://benjamindehli.github.io/sidstation-asid/#download) above. The macOS build is signed and notarized. The Windows and Linux builds are provided but not yet hardware-tested.

### macOS

macOS 10.15 or later, Apple Silicon or Intel. The package is signed and notarized, so it opens without a Gatekeeper warning.

1. Open the DMG from [download](https://benjamindehli.github.io/sidstation-asid/#download) above.
2. Run the installer inside. It asks for your admin password, since the plugins go in the system plugin folders.
3. Restart your DAW. Logic revalidates the AU on the next launch, which takes a moment the first time.
4. Connect the SidStation over MIDI and put it in ASID mode from the front panel.
5. In the plugin, hit Scan and pick the SidStation as the MIDI output.

| What | Goes to |
| --- | --- |
| AU | `/Library/Audio/Plug-Ins/Components` |
| VST3 | `/Library/Audio/Plug-Ins/VST3` |
| Standalone | `/Applications` |

### Windows

1. Run the setup .exe from [download](https://benjamindehli.github.io/sidstation-asid/#download) above. It is unsigned, so SmartScreen may warn: choose More info, then Run anyway.
2. The installer places the VST3 in `Common Files\VST3` and the Standalone in `Program Files`.
3. Restart your DAW so it picks up the new VST3.
4. Connect the SidStation, put it in ASID mode, then Scan and pick it as the MIDI output in the plugin.

### Linux

1. Extract the .tar.gz from [download](https://benjamindehli.github.io/sidstation-asid/#download) above.
2. Copy `SidStation ASID.vst3` into `~/.vst3` (or `/usr/lib/vst3` for all users), and run the Standalone from the extracted folder.
3. Connect the SidStation, put it in ASID mode, then Scan and pick it as the MIDI output in the plugin.

One instance drives one voice, so load three instances on three tracks and set each to a different voice. [DAW setup](https://benjamindehli.github.io/sidstation-asid/daw-setup/) walks through that in Ableton Live, Logic Pro, Cubase, Reaper, Bitwig and FL Studio, including getting the SidStation's audio back into the mix. Pro Tools is the exception, since it loads AAX only. If the unit stays silent, or only one note sounds, [troubleshooting](https://benjamindehli.github.io/sidstation-asid/troubleshooting/) covers what usually causes it.

## Questions

### Does it need a SidStation, or is it an emulator?

It needs the hardware. The plugin contains no SID emulation and makes no sound on its own. It streams register writes to an Elektron SidStation over MIDI, and the sound comes out of the SidStation's own outputs, so its DAW track stays silent by design.

### Does it work with other ASID devices?

It has only been tested with an Elektron SidStation on OS 1.11 R34. The plugin sends standard ASID messages, so other hardware that accepts ASID may respond, but that has not been tried. Some of what the plugin does is tuned to how the SidStation behaves, such as how fast it sends updates and how it ends each frame, and another device could need different settings. Reports from other ASID hardware are welcome on [the issue tracker](https://github.com/benjamindehli/sidstation-asid/issues).

### Which DAWs does it work in?

Any host that loads VST3 or AU plugins. [DAW setup](https://benjamindehli.github.io/sidstation-asid/daw-setup/) walks through Ableton Live, Logic Pro, Cubase, Reaper, Bitwig and FL Studio. Pro Tools is the exception, since it loads AAX only. There, run the Standalone app next to it, or load the VST3 inside an AAX plugin host.

### Can it play chords?

One instance drives one SID voice, so a single instance is monophonic. For chords or three separate parts, put an instance on each of three tracks and pick a different voice on each. The SID chip has three voices, so three notes at once is the limit.

### Can I bounce or export the SidStation in my DAW?

Not with an offline or faster than real time bounce, because the sound is made by the hardware and the plugin's own track is silent. Record the SidStation's audio outputs to a track first, then bounce as usual.

### Is it free?

Yes. It is free and open source under the GNU GPL v3, with builds for macOS, Windows and Linux. The macOS build is signed and notarized. The Windows and Linux builds are not yet tested against hardware.

## Build from source

The project builds via CMake, and JUCE is fetched automatically by the root CMakeLists. With copy after build on, the AU and VST3 land in your user plugin folders, and the Standalone app is the quickest way to test.

```
git clone https://github.com/benjamindehli/sidstation-asid.git
cd sidstation-asid
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Developed and tested against OS 1.11 R34 hardware on macOS. The Windows and Linux builds compile and package but are not yet hardware-tested.

Talking to the hardware turned up a fair amount the manual does not cover, including the reason this plugin uses ASID at all: Direct Program is dead on OS 1.11 R34. The [ASID protocol notes](https://benjamindehli.github.io/sidstation-asid/protocol/) write up the frame format, the two address spaces, and what four real patch banks revealed, and [ASID timing](https://benjamindehli.github.io/sidstation-asid/asid-timing/) covers the write behaviour anything driving the unit from a DAW has to work around.
