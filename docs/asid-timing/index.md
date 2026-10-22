<!-- Generated from docs/asid-timing/index.html by packaging/make-markdown.py. Edit the HTML, then run make markdown. -->

# ASID timing

Getting the frames right is the easy half. This is the behaviour you have to design around.

Web page: https://benjamindehli.github.io/sidstation-asid/asid-timing/

## Writes apply one message late

This is the single most important thing on this page, and everything below is a consequence of it. A write you send does not take effect when it arrives. It takes effect when the *next* message arrives behind it.

A lone write therefore hangs pending, indefinitely, until something else happens to follow it. That is why a note-off can leave a voice sounding forever, and why a correct-looking stream produces the previous note instead of the current one.

For gating, the way through is to write the control register twice inside a single frame, through its primary slot and then its secondary slot, so the unit applies them in order within that one frame. A note-on writes gate low then high, retriggering the envelope atomically. A note-off writes gate low twice. A plain single control write is not applied reliably. In this codebase that is `encodeAsidDoubleControl`.

## About one update per SID frame

The unit applies roughly one ASID update per SID frame, near 50 Hz at PAL. Exceed that and updates are dropped, not queued.

The trap is sending each modulation source as its own frame at its own rate. Pitch, pulse width, cutoff and a wavetable each streaming independently will overrun the limit as soon as two of them run fast, and the symptom is not a clean slowdown: one source starves and lurches between values while another looks fine.

Run one modulation clock for the whole voice instead. Each tick, sample every source, collect the SID registers that actually changed, and send a single combined frame. The frame rate is then fixed regardless of how many modulations are active.

## End every frame with the control register

Because of the one-message-late rule, the last slot in a frame is the one left pending. So the last slot should be the one you least mind waiting, and it should be something that flushes everything before it.

The failure this prevents is subtle. SID frequency is 16 bit across two registers. A frame writing only the two frequency bytes leaves the high byte last, so the oscillator runs with the new low byte and the *old* high byte until the next frame arrives, which is a wrong intermediate pitch. On one voice the next frame comes on schedule and it is barely audible. With two voices interleaving, the flush lands at uneven times and the glide sounds sluggish, worse at lower modulation rates. The MIDI meter stays low throughout, because it is a timing problem and not a traffic one.

Ending each frame with the voice's control register, the highest slot, flushes the frequency and pulse width bytes ahead of it within the same frame. Note-on frames never had the problem because they already end in control writes.

## Every sounding voice has to keep streaming

A voice that modulates sends a steady flow of frames. A voice merely holding a note sends almost nothing. Put those two together and the quiet voice is the one that breaks: its sparse writes get flushed at unpredictable times by the busy voice's traffic.

The shape of the bug is a good tell. No modulation on either voice is fine. Modulation on both is fine. Modulation on exactly one mangles the other. Keeping every sounding voice streaming its frequency each tick, modulated or not, evens out the frame flow and they all flush cleanly.

## The 6581 envelope can swallow a note

The envelope generator shares one rate counter. If a new attack arrives when that counter is parked past the new period, the counter has to run its entire 32767 count sequence before the envelope moves at all. At the PAL clock that is up to about 33 ms, and the note is silent for it.

It tracks the **decay** setting rather than the envelope level, which is what makes it confusing to diagnose. Decay 0 never drops.

Three things help, and they compose:

At sustain 15 the decay is inaudible, since the envelope decays from peak to peak. Sending decay 0 there is sonically identical and starves the bug of its input.

For a fresh attack landing while a previous note still rings, drain first: force the release nibble to 0 and take the gate low, then flush that write a few milliseconds later so it actually applies, then attack. Note that a drain is only worth doing if there is room ahead of the note for it. Stealing that room by delaying the note is worse than not draining, because the note's own modulation frames will gate the voice on early and the drain's gate-low then cuts it, heard as a burst and then silence.

After a release has finished fading, park the release nibble at 0. It is inaudible, since the envelope is already at zero, but it leaves the counter cycling on a short period, so the next attack needs no wrap however long the gap. Where draining needs room before a note, this uses the time after one, of which there is plenty.

Do not retrigger on legato. If a note is already sounding on that oscillator, retune it by writing frequency only and leave the gate high. Reloading the rate counter is what creates the risk, so a note that never reloads it cannot stall.

What is left of the bug after all three, and what to do about it while playing rather than while coding, is in [troubleshooting](https://benjamindehli.github.io/sidstation-asid/troubleshooting/#dropped-notes).

## Do not send MIDI from the audio thread

Sending real-time MIDI from inside the audio callback perturbs the host's audio and MIDI clock. In Logic on the Mac's built-in audio it raises "Error while trying to synchronize Audio and MIDI", and it fires with tiny traffic, independent of buffer size. Standalone stays clean, so it is not load.

Push each frame, with its absolute send time, into a ring buffer and let a dedicated sender thread drain it into a time ordered queue and do the actual device I/O. The audio callback then never touches the MIDI API.

One detail in that queue is easy to miss and expensive to debug: **break ties by an insertion sequence number, not by time alone**. A note-on emits several frames sharing one timestamp, and a plain priority queue is free to reorder equal keys. Reordering them scrambles the one-message-late mechanism, and the audible result is the *previous* note playing on fast passages, masked whenever a modulation stream happens to supply follow-up messages.

## You may not get a host clock

Logic's AU does not give JUCE a per block host timestamp. It is absent in every transport and selection state, so wall clock scheduling against the host is simply not available there. The playhead position is available, and that is what you have to build on.

That matters more than it sounds, because hosts do not render every instrument track at the same point relative to the playhead. Logic renders the selected track live and other instrument tracks ahead of it, so sending immediately makes unselected tracks play early, and two instances disagree purely because of which track you clicked on.

The way out is to schedule against the playhead rather than against arrival: have the instances share the minimum offset between playhead and wall clock, which is the live track's mapping, and schedule every note against that shared reference. Ahead tracks then delay themselves back into line. A user facing latency trim on top absorbs the remaining offset against the audio path.

All of this was validated against one unit on OS 1.11 R34, in Logic and Standalone. The implementation is in [core/](https://github.com/benjamindehli/sidstation-asid/tree/main/core) and the plugin's processor, and the frame format itself is in the [ASID protocol notes](https://benjamindehli.github.io/sidstation-asid/protocol/). If your firmware behaves differently, [say so on the issue tracker](https://github.com/benjamindehli/sidstation-asid/issues).
