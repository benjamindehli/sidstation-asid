# Architecture

How SidStation ASID is put together and why it is put together that way. If you are looking for what the plugin does, the [README](README.md) covers that. If you are looking for what the hardware does, the [protocol notes](https://benjamindehli.github.io/sidstation-asid/protocol/) cover that.

## The shape of the repository

| Directory        | What it holds                                                                                                       |
| ---------------- | ------------------------------------------------------------------------------------------------------------------- |
| `core/`          | The protocol library. C++17, no JUCE, no audio, no GUI, no dependencies, and its own tests.                         |
| `asid/`          | The plugin: processor, editor, MIDI device layer, and the JUCE linked tests.                                        |
| `probe/`         | A command line tool for poking the protocol at real hardware without launching a DAW.                               |
| `packaging/`     | Installers, notarization, and the Python that generates the docs site's images and keeps its version stamps honest. |
| `docs/`          | The GitHub Pages site, served as static files straight from `main`.                                                 |
| `release-notes/` | One file per release, linked from `CHANGELOG.md`.                                                                   |

The split that matters is `core/` against `asid/`. Everything about the SidStation protocols lives in `core/` and can be built and tested with a compiler and nothing else, which is why `make -C core test` runs in a second and needs no JUCE checkout. Everything that needs a plugin framework lives in `asid/`.

## The core library

`core/` encodes the SidStation MIDI, SysEx and ASID protocols as plain data and functions. `Parameters.cpp` is the single source of truth mapping every editable parameter to its address, range, MIDI CC and named choices. `Asid.h` and `Asid.cpp` build the register frames. `Patch.cpp` handles the patch dump codec, `SyxFile.cpp` the .syx files, `VoiceEngine` the note stack, and `Lfo` and `WaveTable` the modulation sources that the SID chip does not provide.

None of it knows what a DAW is. That is deliberate: protocol bugs are much easier to find in a test than in a plugin, and `tests/tests.cpp` is a dependency free harness that runs several hundred checks over the byte sequences.

## The plugin

One instance drives one SID voice. The SidStation has three voices, so three instances on three tracks give you three independently sequenced monosynths. That is the central design decision and most of the rest follows from it.

`AsidProcessor` owns a voice: its parameters, its note scheduling, its modulation clock, and the frames it emits. `AsidEditor` is the interface. `SidLookAndFeel` and `WindowChromeLnF` carry the Commodore 64 styling.

The processor writes no audio. `processBlock` clears its buffer and returns, because the sound is made by the SID chip in the hardware and arrives at the SidStation's own outputs, not through the host.

## AsidShared

Some things belong to the chip rather than to a voice. There is one filter, one master volume, one modulation clock. `AsidShared` is a process wide singleton that lets every instance see those, so changing the cutoff in one instance moves it in all of them.

It also carries two things that are less obvious. It holds the shared timing reference the instances schedule against, described below. And it runs a watchdog that releases a voice whose instance has stopped being called, so a stuck note does not outlive the track that made it.

The honest limitation: this works because instances share a process, which is the normal case for VST3 and in process AU. A host that sandboxes each instance into its own process would not share any of it.

## MidiHub, and why the plugin opens its own port

The plugin does not ask the host to route MIDI to the hardware. `MidiHub` opens the output device itself. That is what makes the behaviour the same whether you are in a DAW or in the Standalone build, and it is why the port is chosen inside the plugin rather than in your host's routing.

Sending is off the audio thread. Real time MIDI sent from inside the audio callback perturbs the host's audio and MIDI clock, which in Logic surfaces as a synchronization error even under tiny traffic. So the audio callback copies each frame, with its absolute send time, into a buffer and returns, and a sender thread owned by the hub does the device I/O at each frame's due time. The guarded section is a bounded copy with no allocation and no syscall.

One detail in the sender is load bearing: frames are ordered by time and then by an insertion sequence number. A note on emits several frames sharing a timestamp, and reordering them breaks the hardware's write behaviour in a way that plays the previous note.

## Scheduling against the playhead

Hosts do not render every instrument track at the same point relative to the playhead, and at least one major host gives an AU no per block wall clock at all. Sending each note the moment it arrives therefore makes some tracks play early and makes two instances disagree based on which track is selected.

Instead, every instance reports the offset it sees between the playhead and the wall clock, `AsidShared` keeps the minimum, and each note is scheduled against that shared reference. Tracks rendered ahead delay themselves back into line. The Latency control is the manual trim on top, for the offset between MIDI reaching outboard hardware and audio coming out of the host.

## Why ASID rather than the documented path

The manual documents a SysEx Direct Program message that sets one parameter at a time, and on the firmware this was developed against that message does nothing at all. ASID, which streams raw SID register writes, does work, and it reaches parameters that MIDI CC cannot touch while giving each voice its own frequency and gate.

That is not a preference, it is what the hardware left available. The [protocol notes](https://benjamindehli.github.io/sidstation-asid/protocol/) and [ASID timing](https://benjamindehli.github.io/sidstation-asid/asid-timing/) pages record the findings behind it, including the write behaviour the whole design has to work around.

## Building and testing

The plugin builds with CMake, which fetches JUCE itself:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The core tests build with a compiler and a Makefile, no CMake and no JUCE:

```sh
make -C core test
```

CI runs three jobs. Formatting and lint first, because it is the cheapest signal. Then the core tests on Linux. Then a macOS job that configures with `-DSID_BUILD_TESTS=ON` and builds `SidStationAsidTests`, a console target that compiles the full processor and editor and links the core, so it validates that the plugin compiles as well as running the JUCE linked tests.

## The docs site and its tooling

`docs/` is served by GitHub Pages as static files with no build step, so anything generated has to be committed. Three scripts keep that honest, all run through the root `Makefile`: `make-screenshots.py` encodes the responsive AVIF and WebP sets from the full resolution sources in `assets/screenshots/`, `check-links.py` resolves every local link so a page moving between directories cannot silently 404, and `stamp-docs.py` keeps the version and dates in step. Each has a check mode that CI runs.

## What is deliberately not here

There is no SID emulation. The plugin makes no sound without the hardware and is not intended to.

There is no MIDI input path. The plugin streams one way. The protocol side for reading from the unit still lives in `core/` and is exercised by `probe/` and the core tests, but the plugin does not use it.

There is no patch librarian. The patch dump codec exists and round trips, but mapping each dump byte to a named parameter is a second table that has not been built.
