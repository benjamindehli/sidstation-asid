# sidprobe, SidStation hardware validation tool

A small command line tool that sends messages to a real SidStation and decodes what comes back, so the core protocol library can be confirmed against hardware before the plugin builds on top of it. It links the same `core` library the plugin uses.

On macOS it uses CoreMIDI for real I/O. On other platforms it builds a dry run backend that prints the bytes it would send, which is useful for developing and testing the command and encoding layer anywhere.

## Build

```sh
cd probe && ./build.sh
```

Or with CMake, which produces `probe/build/sidprobe`:

```sh
cmake -B build && cmake --build build
```

## Usage

```sh
./sidprobe list                        # list MIDI ports
./sidprobe                             # interactive prompt, auto picks a "SID" port
./sidprobe --out 1 --in 2              # choose ports explicitly
./sidprobe send filter.cutoff 64       # one shot command
```

The interactive commands are `list`, `params [filter]`, `send <id> <value>`, `cc <id> <value>`, `note <n> [vel] [ch]`, `off <n> [ch]`, `raw <hex...>`, `skip`, `help`, and `quit`.

Incoming SysEx is decoded and printed automatically. Patch dumps show the name, byte count, and a round trip check. Connect the SidStation MIDI OUT to your interface IN so replies are captured.

`params` lists all parameter ids from the registry. `send` uses Direct Program messages, which cover every parameter. `cc` uses the MIDI CC where one exists.

## Hardware validation checklist

Run these against the unit to close the open questions in `core/README.md`.

Confirm DP encoding is correct. With the SidStation showing a patch, run `send filter.cutoff 0` up to `127` and watch the cutoff move. Repeat for a few parameters across oscillators and LFOs. This confirms addresses and masks.

Confirm bipolar encoding. Run `send osc1.transpose -12`, then `12`, then `0`, and confirm the pitch shifts symmetrically. This validates the 7 bit two's complement.

Confirm patch dump framing. Trigger a patch dump from the SidStation front panel and read the printed bytes. Confirm the size field value against the actual data length, and whether the 10 byte name region is raw ASCII or nibble split. The tool prints a round trip re encode comparison to flag any mismatch.

Confirm voice play gate behaviour, the key one. Set an oscillator to a fixed note with `send osc1.pitchTrack <note>` and send a `note`. Observe whether the SID envelope and gate retrigger, and how the three oscillators respond. This decides the design of the three voice play engine.

Record findings back into `core/README.md`.

## Note about MIDI interfaces

Using an instrument as a USB to DIN MIDI interface often fails for SysEx, because many instruments forward note and CC data but drop or mangle SysEx. A dedicated class compliant USB MIDI interface is the reliable choice. Avoid the cheap no name USB to MIDI cables, they corrupt SysEx too.
