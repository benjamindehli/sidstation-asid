<!-- Generated from docs/troubleshooting/index.html by packaging/make-markdown.py. Edit the HTML, then run make markdown. -->

# Troubleshooting

The things that go wrong, and which of them are the plugin, the SID chip, or the unit itself.

Web page: https://benjamindehli.github.io/sidstation-asid/troubleshooting/

## Nothing plays at all

Four things have to be true, and the first two catch almost everything.

### The unit has to be in ASID mode

Switch it on from the SidStation's own front panel. Nothing sent over MIDI can put it there, so a perfectly correct ASID stream arriving at a unit in its normal mode produces silence and no error anywhere.

### The MIDI output is picked in the plugin, not routed in the DAW

The plugin opens the MIDI port itself rather than relying on the host to route MIDI to the hardware, which is what makes it behave the same way regardless of the host, and in Standalone. Press Scan in the plugin's top strip and choose your interface there. Routing the plugin's MIDI output to the SidStation in your DAW instead will not do it.

### The sound comes out of the SidStation, not the plugin

The plugin produces no audio. It contains no SID emulation, so the audio you are listening for is at the SidStation's own outputs, and the plugin's track in the DAW stays silent by design.

### You need the hardware

There is no demo mode and no emulation fallback. Without an Elektron SidStation connected there is nothing to hear.

## Only one note sounds at a time

That is the design. One instance drives one SID voice, so a single instance is monophonic no matter what you play into it.

For three note chords, put an instance on each of three DAW tracks and set a different voice on each from the voice selector in the top strip. The three voices then behave as three monosynths you can sequence apart, each with its own pitch, gate and envelope.

The filter, master volume and cutoff modulation are shared, because the SID chip has one of each. Changing them in any instance changes them for all three. [Sound design](https://benjamindehli.github.io/sidstation-asid/sound-design/) covers what to do with a voice once it is playing, including using voice 3 as a modulator rather than as a third note.

## Notes are slightly sharp

Versions before 1.2.1 played every note about 26 cents sharp, roughly a quarter of a semitone. It was the same on every voice and waveform, with or without pitch modulation. Updating to [1.2.1 or later](https://benjamindehli.github.io/sidstation-asid/#download) fixes it.

Notes were converted to SID frequency values using the clock of a PAL Commodore 64, which runs at 985248 Hz, but the SidStation runs its SID at 1 MHz. With Coarse and Fine centred, A4 now plays at 440 Hz.

A preset you tuned by ear to make up for the old offset now plays about 26 cents flat. Raising its Fine control by 26 cents restores the old pitch.

## Notes drop out on fast passages

This is a real MOS6581 hardware bug, not the plugin missing a message. The envelope generator shares one rate counter, and a fast retrigger can arrive while that counter is parked at a high value. The next attack then has to wait for the counter to run its whole sequence before the envelope moves, which at PAL speed is up to about 33 ms, and the note comes out silent.

What drives it is the **decay** setting, not how loud the note was. Decay 0 never drops. Higher decay values drop more.

The plugin already clears the common case. At sustain 15 the decay is inaudible, since the envelope decays from peak to peak, so it sends decay 0 there and greys the knob out. Nothing about your sound changes and the bug loses its input.

What is left is a genuine hardware limit: a high decay with sustain below 15, where the decay is audible and cannot be zeroed for you. On fast parts, keep decay lower.

There is a second defence worth knowing about, because you control how well it works. When a new note arrives while the previous one is still releasing, the plugin drains the envelope to zero just before the attack, so the counter is cycling on a short period by the time the note starts. That drain needs a few milliseconds of room ahead of the note, and it only ever uses room that already exists, from the host's render lookahead or from the Latency you have dialled in. It never delays the note to make room. So if notes still drop on fast parts, raising Latency gives the drain somewhere to happen.

The measurements behind all of that, and the rest of what the counter does, are in [ASID timing](https://benjamindehli.github.io/sidstation-asid/asid-timing/#envelope).

## Modulation sounds uneven, or the MIDI meter is pegged

In ASID mode the unit applies roughly one update per SID frame, about 50 per second at PAL. Push more than that at it and updates are dropped rather than queued, which sounds like modulation lurching between values instead of gliding.

Clock in the top strip is the dial. **PAL 50 Hz is the sweet spot** and the default. HiFi 100 Hz sends faster than the unit can apply and can drop. Eco 25 Hz halves the traffic.

The MIDI load meter matters because all three voices share one MIDI port, and it is not a fast one. If the meter is high, lower Clock before suspecting anything else. [ASID timing](https://benjamindehli.github.io/sidstation-asid/asid-timing/#update-rate) has the measured rates the Clock settings are picked from.

## Notes sit early or late against the rest of the mix

MIDI to outboard hardware never lands at the same instant as audio rendered inside the DAW. Latency in the top strip is the manual trim: it adds milliseconds to each note's scheduled time, so you can push the SidStation back into line with everything else.

If several instances drift apart from each other rather than from the mix, that is a different problem and the plugin handles it. Hosts do not render every instrument track at the same point relative to the playhead, so the instances share a common reference and schedule their notes against it. Latency is itself a shared setting, so changing it in one instance changes it in all of them and they stay together on their own.

Panic releases every voice, which is the quickest way out of a note left hanging by a crash or an interrupted transport.

## A patch editor or Direct Program does nothing

On OS 1.11 R34 firmware the documented SysEx Direct Program path is dead. The messages arrive intact and the unit ignores them, which is exactly what it looks like when you have the address wrong. Reading patch dumps over SysEx does still work on the same firmware, and so does ASID. Writing a patch back to the unit is untested on this firmware.

If you are writing something of your own against the SidStation, the [ASID protocol notes](https://benjamindehli.github.io/sidstation-asid/protocol/) cover that finding, the frame format, the two address spaces, and what four real patch banks turned up.

Something here not matching your unit, particularly on different firmware, is worth reporting on [the issue tracker](https://github.com/benjamindehli/sidstation-asid/issues). All of this comes from one unit on OS 1.11 R34.
