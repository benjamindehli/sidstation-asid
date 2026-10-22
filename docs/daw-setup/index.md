<!-- Generated from docs/daw-setup/index.html by packaging/make-markdown.py. Edit the HTML, then run make markdown. -->

# DAW setup

Three instances, three tracks, and the audio coming back from the hardware. The same shape in every host.

Web page: https://benjamindehli.github.io/sidstation-asid/daw-setup/

## The same in every host

Two things about this plugin decide how a session is put together, and neither is how a software instrument normally behaves. Get them straight once and the host specific parts are small.

**The plugin opens the MIDI port itself.** Press Scan in the top strip and choose your interface there. Do not route the track's MIDI output to the SidStation in the host as well. The port is shared by every instance in the session, so you pick it once, in whichever instance you happen to have open, and the others follow.

**The plugin makes no sound.** Its track stays silent by design, because the audio is at the SidStation's own outputs. Getting that back into the mix is a separate audio track fed by the interface input the unit is plugged into. Nothing you do to the plugin's track affects what you hear.

Everything else follows from one instance driving one voice:

1. Connect the SidStation over MIDI, and its audio output to an input on your interface.
2. Put it in ASID mode from its own front panel. Nothing sent over MIDI can do this for you.
3. Load an instance on each of three instrument tracks and set a different voice on each from the top strip.
4. In any one of them, press Scan and pick the SidStation as the MIDI output.
5. Add an audio track whose input is where the SidStation is plugged in, and turn its monitoring on.

Three tracks is only worth it if you want three voices. One instance is a perfectly good monosynth, and a lot of SID music is one voice doing the work while the other two hold a chord.

## What the three instances share

The SID has one filter and one volume for all three voices, so those controls are shared between the instances rather than copied. Changing the cutoff in one moves it in all three, and it moves in the hardware once. That is the chip, not a limitation of the plugin.

Shared: the filter (cutoff, resonance, per voice routing, external input, and the LP, BP and HP modes), master volume, the cutoff modulation LFO, Voice 3 Off, Clock and Latency. Everything that belongs to one voice's sound stays local to its instance, which is the oscillator, the envelope, the wavetable and the pitch and pulse width LFOs.

Presets follow the same line. A preset is one voice's sound, so loading one on a track leaves the other two voices alone and does not move a track onto a different voice. [Sound design](https://benjamindehli.github.io/sidstation-asid/sound-design/#presets) has the detail.

## Host by host

What differs between hosts is the plugin format, and where the audio return lives. Menu names move between versions, so these are the parts that matter rather than a click by click path.

### Logic Pro

Use the AU. Logic does not load VST3 at all, so the AU is the only format that will appear, under DehliMusikk in the instrument list. Logic validates new Audio Units on the first launch after installing, which takes a moment and only happens once.

Three software instrument tracks, one instance each. For the audio return, an audio track or an aux with its input set to the interface input, with software monitoring on. Reach for the External Instrument plugin only if you know why you want it: it exists to send MIDI to hardware and bring audio back, and the MIDI half of that job is already done inside this plugin.

### Ableton Live

Use the VST3, or the AU on macOS. Three MIDI tracks, one instance each. Add an audio track, set its input to the interface input carrying the SidStation, and set monitoring to In so you hear it whether or not the track is armed.

If the plugin does not show up after installing, rescan in the plugin preferences. Live caches what it found last time.

### Reaper

Use the VST3, or the AU on macOS. Three tracks with an instance each, and a fourth track with the hardware input selected, record armed and monitoring on, which is how Reaper passes an input through to the mix.

Reaper rescans on demand rather than automatically, so scan for new plugins if it is not in the list.

### Bitwig Studio

Use the VST3. Three instrument tracks, one instance each, and an audio track with the hardware input for the return. The Hardware Instrument device is not the route here, for the same reason as Logic's External Instrument: the plugin sends its own MIDI.

### Cubase and Nuendo

Use the VST3. Steinberg's hosts are VST only, so there is no AU to look for on macOS. Three instrument tracks, one instance each, and an audio track with the hardware input for the return, monitoring on.

Cubase has its own External Instrument, set up in the audio connections rather than as a plugin. Same answer as Logic: it exists to send MIDI out to hardware and bring audio back, and this plugin already sends its own MIDI, so a plain audio track is the simpler return.

### FL Studio

Use the VST3, or the AU on macOS. FL only finds new plugins when you tell it to, so run a scan in the plugin manager if it is not in the list. Three instances, one voice each, and a mixer track with the interface input assigned for the return.

One thing to watch, since this plugin is silent by design: if you use Smart Disable, and instances seem to stop streaming to the unit when they are not receiving notes, turn it off for them. A plugin that never produces audio is exactly the sort of thing that kind of feature is looking for.

### Pro Tools

**It does not load.** Pro Tools takes AAX and nothing else, and this plugin ships AU, VST3 and Standalone. There is no AAX build, and there is no setting that will make one appear.

Two ways around it. Run the Standalone app next to Pro Tools, play the SidStation from it, and record the audio return into the session like any other outboard synth. Or load the VST3 inside an AAX plugin host, of which a few are sold, and which pass MIDI and audio through to a plugin Pro Tools cannot see by itself.

Building an AAX version is not a matter of ticking a format in the build. It needs Avid's SDK, and a plugin has to be signed through PACE before a released Pro Tools will load it at all, which is a wall a free project does not get over on its own.

### Standalone

The Standalone app skips all of this. There is no host, so set the tempo by hand in the top strip, and monitor the SidStation however you normally would. It is the quickest way to check the unit is talking before you build a session around it.

## Lining the hardware up with the mix

MIDI to outboard hardware never lands at the same instant as audio rendered inside the DAW, and the plugin does not report a latency figure to the host for it to compensate, because the delay is in the cable and the unit rather than in the plugin. Latency in the top strip is the manual trim: 0 to 500 ms added to each note's scheduled time, so you can push the SidStation back into line with everything else.

Set it by ear against a steady reference. It is shared across the instances, so trimming one trims all three and they stay together on their own. If the instances drift apart from each other rather than from the mix, that is already handled: hosts do not render every track at the same point relative to the playhead, so the instances schedule against a common reference.

A little Latency is also worth having on fast parts for an unrelated reason. It gives the plugin room to clear the 6581 envelope before an attack, which is what stops notes disappearing. [Troubleshooting](https://benjamindehli.github.io/sidstation-asid/troubleshooting/#dropped-notes) covers that, and [ASID timing](https://benjamindehli.github.io/sidstation-asid/asid-timing/#envelope) covers why the chip does it.

Anything a host does to save CPU on a silent track is worth knowing about, because a sounding voice has to keep being streamed to. Freezing an instance, disabling its track, or a feature that suspends plugins producing no audio all cut the stream, and the note that was playing goes with it. [ASID timing](https://benjamindehli.github.io/sidstation-asid/asid-timing/#even-flow) covers why the streaming has to continue.

Bouncing is the other consequence of the audio living outside the DAW. An offline or faster than real time bounce will not capture the SidStation, since the hardware plays in real time and only what arrives at your interface input can be recorded. Record the audio return to a track first, then bounce as usual.

## Clock, and how hard to push the port

All three voices share one MIDI port, and it is not a fast one. Clock in the top strip sets how often the plugin sends: Eco 25 Hz, PAL 50 Hz, NTSC 60 Hz and HiFi 100 Hz. PAL 50 Hz is the default and the native SID rate, and it is the right answer almost always.

Going higher does not buy smoother modulation. The unit applies roughly one update per SID frame and drops the rest rather than queuing them, so HiFi mostly adds traffic. Watch the MIDI load meter in the top strip: if it is high, come down a setting. Eco 25 Hz halves the traffic and is there for busy sessions.

Tempo synced wavetable steps and LFO rates read the host tempo, so they follow tempo changes in the session with no setup. If a host reports no tempo at all, the plugin notices and gives you an editable BPM field instead, the same one Standalone uses. [ASID timing](https://benjamindehli.github.io/sidstation-asid/asid-timing/#update-rate) has the measurements behind the Clock settings.
