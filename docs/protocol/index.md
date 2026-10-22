<!-- Generated from docs/protocol/index.html by packaging/make-markdown.py. Edit the HTML, then run make markdown. -->

# ASID protocol

What talking to a real Elektron SidStation turned up, and why this plugin works the way it does.

Web page: https://benjamindehli.github.io/sidstation-asid/protocol/

## Direct Program is dead on OS 1.11 R34

The SidStation Owners Manual (r22b, OS 1.1) documents a SysEx Direct Program message that sets a single parameter at a live memory address. It is the obvious way to drive the unit from software, and the documentation gives no hint that it might not work.

On OS 1.11 R34 firmware the unit does not honour it. Direct Program messages arrive intact, and the unit ignores them. That was confirmed by sending a cutoff and a transpose value, storing the patch on the unit and dumping it back: the patch still read the previously stored values, so Direct Program reaches not even the edit buffer. Reading patch dumps over SysEx does work on the same firmware, and so does ASID mode, which is what made the difference. This single finding is the reason the plugin streams raw SID registers rather than editing patch parameters, and it is worth knowing before you spend time building against the documented path.

## ASID: streaming raw SID registers

ASID bypasses the SidStation patch engine entirely and writes the MOS6581 chip registers directly. That reaches everything the chip can do, including parameters MIDI CC cannot touch (waveform selection, resonance, filter mode), and it gives genuinely independent per-voice play, because each SID voice has its own frequency and gate.

The frame, confirmed against the Jouni Paulus specification and the vap decoder:

```
F0 2D 4E [mask1..4] [msb1..4] [data...] F7
```

Four mask bytes carry 7 bits each and flag which of up to 28 register slots are being written. Four msb bytes carry the eighth bit of each register value, since SysEx data bytes only hold 7 bits. Then one 7-bit data byte per flagged slot, in ascending slot order.

The unit has to be put into ASID mode from its front panel first. Nothing on the MIDI side can do it for you, which is the most common reason a correctly built ASID stream produces silence.

Getting the frame right is only half of it. When the writes land, how many of them the unit will take, and what the envelope does to a fast retrigger are in [ASID timing](https://benjamindehli.github.io/sidstation-asid/asid-timing/).

## Two address spaces

The SidStation exposes its parameters through two different memory layouts, and they do not agree with each other.

### Direct Program map

Sparse live memory addresses, one parameter per message. Oscillator 1 sits at base 0x47 with a 21 byte stride, and LFO 1 at base 0x86 with a 28 byte stride.

### Patch dump layout

A compact structure of 143 bytes and up, used for a whole patch, nibble encoded on the wire. Oscillator 1 sits at index 36 and LFO 1 at index 99.

The first 0x24 bytes (name, direct controllers, filter, mode) coincide between the two. Oscillators and LFOs diverge, so a mapping built for one layout will silently produce wrong values in the other.

One documented range is unreachable through Direct Program at all: Pitch Sync Speed is specified as 50 to 200, but its Direct Program field is 7 bit, so anything above 127 can only be set through a full patch dump.

## What real patch banks revealed

The codec was validated against four real SidStation .syx banks (Giraya, Klaus P Rausch, Ninjabank, Presets_r1), roughly 90 to 100 patches each. Four things came out of that which the manual does not tell you.

### Sending a bank is destructive, sending a patch is not

A bank file is one PatchAllClear (0x01) followed by, for each of 128 slots, either a PatchDump (0x02) or a SkipPatch (0x03). Because it opens by wiping patch memory, sending a whole bank file would clear the unit before writing anything. That is read off the file structure rather than tested: sending patches to a unit on this firmware is untested here, and may not work at all, so treat a bank file as destructive and keep a backup either way.

### The size field is unreliable

Different banks disagree on what it means. Giraya writes the byte count. Klaus P Rausch writes the byte count minus 145. The device appears to ignore it and simply parses to F7. A round trip against third party banks therefore differs in those two bytes while the data itself is byte identical.

### There is an older init sequence

SidStation_Presets_r1.syx opens with F0 00 45 01 00 rather than F0 00 20 3C 01 00. It is safer to reject that than to risk mis-parsing it as the current format.

### Bulk sends have to be paced

Elektron's own C6 tool allows 5 to 50 ms between packets. A zero delay burst overflows the unit and fails silently, which looks exactly like a protocol bug and is not one.

## Still open

Typed patch dump field access. The patch codec exposes the name and raw bytes with a validated round trip, but mapping each byte to a typed field is a second table distinct from the Direct Program map, and it is what reading a received patch back into named parameters would need.

Table data for the arpeggiator and waveform sequences, at Direct Program positions from 0x176 up, is not modelled. The plugin runs its own eight step wavetable over ASID instead, which [sound design](https://benjamindehli.github.io/sidstation-asid/sound-design/#wavetable) covers.

All of this comes from one unit on OS 1.11 R34. If your firmware behaves differently, particularly if Direct Program works for you, that is worth reporting on [the issue tracker](https://github.com/benjamindehli/sidstation-asid/issues).

The code behind these notes is the framework agnostic core library in the repository, unit tested against the byte sequences in the manual: [core/](https://github.com/benjamindehli/sidstation-asid/tree/main/core) on GitHub.
