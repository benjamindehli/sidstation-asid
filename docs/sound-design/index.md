<!-- Generated from docs/sound-design/index.html by packaging/make-markdown.py. Edit the HTML, then run make markdown. -->

# Sound design

The wavetable, the three LFOs, and the chip rules that decide which combinations do anything at all.

Web page: https://benjamindehli.github.io/sidstation-asid/sound-design/

## Chip rules that decide what works

Several combinations on the SID do nothing, and none of them tell you so. The plugin greys out a control when the chip would ignore it, which is worth knowing about as a set of rules rather than as a surprise.

**Noise is exclusive.** The four waveforms otherwise combine freely, but noise silences the others on the real 6581, so turning noise on takes the rest out.

**Ring modulation needs the triangle wave, and not noise.** Ring mod on any other waveform does nothing. It modulates against the previous voice, so voice 1 rings against voice 3.

**Hard sync does nothing on a noise-only voice.** Like ring mod, it syncs this oscillator to the previous voice's.

**Pulse width only matters with the pulse wave on.** Obvious in isolation, easy to miss when the pulse wave is one of several combined.

**Decay does nothing at full sustain.** At sustain 15 the envelope decays from peak to peak, so decay is inaudible. The plugin pins it to 0 there, which is sonically identical and has a useful side effect covered in [troubleshooting](https://benjamindehli.github.io/sidstation-asid/troubleshooting/#dropped-notes).

## The wavetable

The SID has no wavetable. This is the software technique the SidStation and the C64 trackers use: rewrite a voice's waveform, and its pitch offset, once per frame. The plugin clocks the table and writes the result, so the table lives here rather than in the chip.

Eight steps, each carrying its own waveform selection, sync, ring, test bit, pulse width and an arpeggio offset in semitones. Three controls govern playback: **Rate** is frames per step, so 1 is a new step on every frame, roughly 50 per second at PAL. **Length** is how many steps play. **Loop** is the step it jumps back to after the last one, which is what separates a one-shot attack from a sustained texture.

**Sync** times a step by a note division at the host tempo instead of by frames, which is what keeps an arpeggio table in time when the tempo changes. Rate becomes the division while it is on and reads as 1/16 and so on, down to 1/64, which at 120 BPM is a 31 ms step, the same territory as the 20 ms PAL frame the table was built around.

Steps can only turn over on a tick of the Clock in the top strip, which sets the ceiling on how short a synced step can usefully be. Two things follow from that. A step lasting a few ticks turns over a tick early or late, and no worse than that, because the leftover time carries into the next step instead of piling up as drift. A step shorter than a single tick is different: the table crosses whole steps between ticks, and a step crossed that way is never written to the chip, so it is skipped rather than shortened. The plugin says so on the Rate control when that is happening. 1/64 is the one to watch: it starts skipping above roughly 94 BPM on Eco 25 Hz and roughly 188 BPM on PAL 50 Hz, while HiFi 100 Hz keeps it clean all the way to 300. Raising the Clock or picking a longer division are the two ways out.

Two classic uses fall out of that. Put semitone offsets on successive steps at rate 1 and you get the C64 arpeggio, a chord implied by one voice cycling through its notes faster than the ear separates them. Or set a short length with the loop pointing past the first steps, so those steps become a transient: a couple of frames of noise or a different waveform at the front of the note, then a loop over the sustained part.

The table restarts at step 0 on each note-on and stops on release, whether or not it is synced: the step length follows the tempo, the position follows the note, the way a tracker table and an arpeggiator do. Switch it off and the oscillator's own waveform, sync, ring and pulse width controls take back over.

## The three LFOs

One on pitch, one on pulse width, one on filter cutoff. The SID has no LFOs of its own, so these run in the plugin and are streamed to the chip as register writes.

Shapes are sine, triangle, saw up, saw down, square, sample and hold, and random. The last two differ in a way worth knowing: sample and hold steps to a new random value each cycle and stays there, while random picks the same values but glides between them. Shape doubles as the on switch, so **Off** is in the same list.

**Rate** is free running in Hz, or a stepped note division when Tempo Sync is on, in which case it locks to the host tempo and reads as 1/4 and so on. **Delay** fades the LFO in after a note starts, which is how you get vibrato that arrives on a held note rather than sitting on the attack. **Mod Wheel** hands control to the wheel, and Depth becomes the maximum it can reach rather than a fixed amount.

The cutoff LFO is shared across the three voices, because the filter is. It also only streams when at least one voice is actually routed through the filter, so a cutoff LFO on an unrouted patch is silent by design rather than broken.

All three run on one modulation clock, set by Clock in the top strip. If the movement sounds like it is stepping rather than gliding, that is the clock rather than the LFO, and [the rate is the thing to change](https://benjamindehli.github.io/sidstation-asid/troubleshooting/#modulation).

## Voice 3 as a modulator

Since ring modulation and hard sync both take the previous voice as their source, a voice can be spent driving another rather than being heard. Voice 3 Off silences it for exactly that: it keeps running as a modulation source for the voice that takes it as one, while contributing nothing audible itself.

That is the trade the chip forces. Three voices, and metallic ring modulated timbres cost you one of them.

## Presets are files

A preset is one voice's sound, saved to disk, outside both the DAW project and the unit's own patch memory. A sound made in one project is available in the next, and nothing is written to the SidStation.

They live under your user application data folder, in DehliMusikk, SidStation ASID, Presets.

What a preset holds is the voice sound: the oscillator, envelope, wavetable and LFO settings. It deliberately leaves out the things that are not part of one voice's sound: everything shared across the three voices, which is the filter, master volume, Voice 3 Off, Clock and Latency, plus the voice selection and the tempo. Loading a preset on an instance therefore does not disturb the other two voices or move a track onto a different voice.

**Init** returns the voice to the default sound, which is the quickest way to stop fighting a patch and start again. Panic, next to it, is the unrelated escape hatch for a note left hanging.
