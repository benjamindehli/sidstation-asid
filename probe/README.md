# sidprobe — SidStation hardware validation tool

A tiny standalone CLI that sends messages to a real SidStation and decodes what
comes back, so we can confirm the core protocol library against hardware before
building the plugin on top of it. It links the same `core/` library the plugin
will use.

On **macOS** it uses CoreMIDI for real I/O. On other platforms it builds a
**dry-run** backend that prints the bytes it would send (useful for developing
and testing the command/encoding layer anywhere).

## Build

```sh
cd probe && ./build.sh            # quick, no CMake
# or:
cmake -B build && cmake --build build   # produces probe/build/sidprobe
```

## Usage

```sh
./sidprobe list                        # list MIDI ports
./sidprobe                             # interactive REPL (auto-picks a "SID" port)
./sidprobe --out 1 --in 2              # choose ports explicitly
./sidprobe send filter.cutoff 64       # one-shot command
```

REPL commands: `list`, `params [filter]`, `send <id> <value>`, `cc <id> <value>`,
`note <n> [vel] [ch]`, `off <n> [ch]`, `raw <hex...>`, `skip`, `help`, `quit`.

Incoming SysEx is decoded and printed automatically (patch dumps show name, byte
count, and a round-trip check). Connect the SidStation's MIDI OUT to your
interface's IN so replies are captured.

> `params` lists all parameter ids from the registry. `send` uses Direct-Program
> messages (covers every parameter); `cc` uses the MIDI CC where one exists.

## Hardware validation checklist

Run these against the unit to close the open questions in `core/README.md`:

1. **DP encoding is correct** — with the SidStation showing a patch, run
   `send filter.cutoff 0` … `127` and watch the cutoff move. Repeat for a few
   parameters across oscillators/LFOs. Confirms addresses and masks.

2. **Bipolar encoding** — `send osc1.transpose -12` / `12` / `0`; confirm the
   pitch shifts symmetrically. Validates 7-bit two's-complement.

3. **Patch-dump framing** — trigger a patch dump from the SidStation front panel
   and read the printed bytes. Confirm: the size-field value vs. actual data
   length, and whether the 10-byte name region is raw ASCII or nibble-split.
   The tool prints a round-trip re-encode comparison to flag any mismatch.

4. **Voice-play gate behaviour (the key one)** — set an oscillator to a fixed
   note via `send osc1.pitchTrack <note>` and send a `note`; observe whether the
   SID envelope/gate retriggers, and how the three oscillators respond. This
   determines the design of the three-voice play engine.

Record findings back into `core/README.md`.
