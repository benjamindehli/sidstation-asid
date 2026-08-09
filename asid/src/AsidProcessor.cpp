#include "AsidProcessor.h"

#include <algorithm>
#include <cmath>

#include "AsidEditor.h"

using namespace sidstation;

namespace {
juce::AudioParameterInt* intParam(const char* id, const char* name, int lo, int hi, int def) {
    return new juce::AudioParameterInt(juce::ParameterID{id, 1}, name, lo, hi, def);
}

// Modulation stream interval in ms for the update-rate choice.
double modIntervalForRate(int idx) {
    switch (idx) {
        case 0: return 40.0;             // Eco 25 Hz
        case 1: return 20.0;             // PAL 50 Hz
        case 2: return 1000.0 / 60.0;    // NTSC 60 Hz
        case 3: return 10.0;             // HiFi 100 Hz
    }
    return 20.0;
}

// Approximate 6581 envelope release time in ms (full-level to zero) per release
// nibble 0..15. Used to tell whether a just-released note is still ringing when a
// new note attacks, which is when the ADSR bug bites and a hard restart is worth it.
double sidReleaseMs(int r) {
    static const double t[16] = {6, 24, 48, 72, 114, 168, 204, 240,
                                 300, 750, 1500, 2400, 3000, 9000, 15000, 24000};
    return t[r < 0 ? 0 : (r > 15 ? 15 : r)];
}

// Note division to beats, for tempo-synced LFO phase.
double beatsForDivision(int idx) {
    switch (idx) {
        case 0: return 4.0;        // 1/1
        case 1: return 2.0;        // 1/2
        case 2: return 1.0;        // 1/4
        case 3: return 2.0 / 3.0;  // 1/4 triplet
        case 4: return 0.5;        // 1/8
        case 5: return 1.0 / 3.0;  // 1/8 triplet
        case 6: return 0.25;       // 1/16
        case 7: return 1.0 / 6.0;  // 1/16 triplet
    }
    return 1.0;
}
}  // namespace

juce::AudioProcessorValueTreeState::ParameterLayout AsidProcessor::makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using Choice = juce::AudioParameterChoice;

    // Which SID voice this instance drives. Saved per instance.
    layout.add(std::make_unique<Choice>(juce::ParameterID{"asidVoice", 1}, "SID Voice",
                                        juce::StringArray{"Voice 1", "Voice 2", "Voice 3"}, 0));

    // Per-voice waveform: the SID's four waveforms combine (their outputs are
    // logically ANDed), so these are independent toggles rather than one choice.
    // Sawtooth on by default matches the player's default voice. Noise cannot be
    // mixed on the 6581 (it locks the other waveforms), so it is treated as
    // exclusive in waveBits().
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"waveTri", 1}, "Triangle", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"waveSaw", 1}, "Sawtooth", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"wavePulse", 1}, "Pulse", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"waveNoise", 1}, "Noise", false));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("attack", "Attack", 0, 15, 0)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("decay", "Decay", 0, 15, 0)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("sustain", "Sustain", 0, 15, 15)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("release", "Release", 0, 15, 0)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("pulseWidth", "Pulse Width", 0, 4095, 2048)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("coarse", "Coarse Tune", -24, 24, 0)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("fine", "Fine Tune", -50, 50, 0)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("pitchBendRange", "Pitch Bend Range", 0, 24, 2)));
    // Portamento: glide time in ms (0 = off), trigger, and stepped/smooth type.
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("portaTime", "Portamento", 0, 2000, 0)));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"portaTrigger", 1}, "Portamento Trigger",
        juce::StringArray{"Legato", "Always"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"portaType", 1}, "Portamento Type",
        juce::StringArray{"Smooth", "Stepped"}, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"sync", 1}, "Sync", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"ring", 1}, "Ring Mod", false));
    // TEST bit: holds the oscillator in reset (silent) while on. Advanced.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"test", 1}, "Test (osc reset)", false));

    // Shared across all three voices (one physical SID filter and master volume).
    // Filter routing is per voice (filt1/2/3) but shared, so any instance can
    // route any voice. Filter mode bits (LP/BP/HP) combine, as the SID allows.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"filt1", 1}, "Filter Voice 1", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"filt2", 1}, "Filter Voice 2", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"filt3", 1}, "Filter Voice 3", false));
    // External audio input through the filter (4th bit of the routing nibble). Shared.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"filtExt", 1}, "Filter External In", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"modeLP", 1}, "Filter Low Pass", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"modeBP", 1}, "Filter Band Pass", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"modeHP", 1}, "Filter High Pass", false));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("cutoff", "Cutoff", 0, 2047, 2047)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("resonance", "Resonance", 0, 15, 0)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("volume", "Volume", 0, 15, 15)));
    // Disconnects voice 3 from the output while it keeps running, so it can be used
    // purely as a ring/sync modulation source ($18 bit 7). Shared, only affects V3.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"voice3off", 1}, "Voice 3 Silent", false));
    // Milliseconds added to each note's scheduled play time, to line the
    // hardware sound up with the DAW's audio output. Shared by all instances.
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("latency", "Output Latency", 0, 500, 0)));

    // Modulation: one plugin-side LFO per target (the SID has none). Pitch and
    // pulse width are per voice; cutoff is the one shared filter. Depth 0 = off.
    auto addLfo = [&layout](const juce::String& prefix, const juce::String& name) {
        using C = juce::AudioParameterChoice;
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{prefix + "On", 1}, name + " On", false));
        layout.add(std::make_unique<C>(
            juce::ParameterID{prefix + "Shape", 1}, name + " Shape",
            juce::StringArray{"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Random"}, 0));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{prefix + "Sync", 1}, name + " Sync", false));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{prefix + "Rate", 1}, name + " Rate",
            juce::NormalisableRange<float>(0.05f, 20.0f, 0.0f, 0.35f), 2.0f));
        layout.add(std::make_unique<C>(
            juce::ParameterID{prefix + "Div", 1}, name + " Division",
            juce::StringArray{"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T"}, 2));
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{prefix + "Depth", 1}, name + " Depth", 0, 100, 50));
        // Mod-wheel control (depth becomes the maximum, scaled by the wheel) and a
        // fade-in delay in ms that ramps the LFO up on each note trigger.
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{prefix + "Wheel", 1}, name + " Mod Wheel", false));
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{prefix + "Delay", 1}, name + " Delay", 0, 4000, 0));
    };
    addLfo("pitchLfo", "Pitch LFO");
    addLfo("pwLfo", "PW LFO");
    addLfo("cutLfo", "Cutoff LFO");

    // One modulation clock for the whole voice: every source (LFOs, glide,
    // wavetable) is sampled on this tick and sent in a single combined ASID
    // frame, so the frame rate stays within what the SidStation applies per SID
    // frame. PAL 50 Hz is the native SID rate; Smooth over-sends and can drop.
    layout.add(std::make_unique<Choice>(juce::ParameterID{"modRate", 1}, "Mod Rate",
        juce::StringArray{"Eco 25 Hz", "PAL 50 Hz", "NTSC 60 Hz", "HiFi 100 Hz"}, 1));

    // Tempo the BPM-synced LFOs lock to when there is no host transport (standalone,
    // or a host that reports no tempo). In a DAW the host BPM is used instead and
    // this is ignored. Saved per instance.
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("bpm", "Tempo (BPM)", 20, 300, 120)));

    // Wavetable: a per-voice table stepped once per PAL frame (~50 Hz), the SID
    // "waveform table" done in software. Each of the 8 steps sets a waveform and
    // an arpeggio offset; the table advances every `wtSpeed` frames and loops.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"wtOn", 1}, "Wavetable On", false));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("wtSpeed", "WT Speed", 1, 16, 2)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("wtLength", "WT Length", 1, kWtSteps, 1)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("wtLoop", "WT Loop", 0, kWtSteps - 1, 0)));
    // Each step's waveform is combinable toggles, like the oscillator (noise is
    // exclusive in wtStepWaveBits). Triangle on by default matches the old default.
    for (int i = 0; i < kWtSteps; ++i) {
        const juce::String s(i), n(i + 1);
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"wtTri" + s, 1}, "WT Tri " + n, true));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"wtSaw" + s, 1}, "WT Saw " + n, false));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"wtPulse" + s, 1}, "WT Pulse " + n, false));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"wtNoise" + s, 1}, "WT Noise " + n, false));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"wtSync" + s, 1}, "WT Sync " + n, false));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"wtRing" + s, 1}, "WT Ring " + n, false));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"wtTest" + s, 1}, "WT Test " + n, false));
        layout.add(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{"wtPw" + s, 1}, "WT Pulse Width " + n, 0, 4095, 2048));
        layout.add(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{"wtArp" + s, 1}, "WT Arp " + n, -24, 24, 0));
    }
    return layout;
}

int AsidProcessor::paramInt(const char* id) const {
    if (auto* p = apvts.getRawParameterValue(id)) return static_cast<int>(std::lround(p->load()));
    return 0;
}

float AsidProcessor::paramFloat(const char* id) const {
    if (auto* p = apvts.getRawParameterValue(id)) return p->load();
    return 0.0f;
}

double AsidProcessor::lfoAmount(const char* prefix, double nowMs) const {
    double amt = paramInt(juce::String(prefix) + "Depth") / 100.0;
    if (paramInt(juce::String(prefix) + "Wheel")) amt *= modWheelValue / 127.0;  // wheel scales depth
    const int delayMs = paramInt(juce::String(prefix) + "Delay");
    if (delayMs > 0) amt *= juce::jlimit(0.0, 1.0, (nowMs - noteOnMs) / delayMs);  // fade in
    return amt;
}

void AsidProcessor::updatePitchOffset() {
    // Pitch wheel: 14-bit, centre 8192, mapped to +-pitchBendRange semitones.
    const double bend = (pitchWheelValue - 8192) / 8192.0 * paramInt("pitchBendRange");
    asidPlayer.setPitchOffset(paramInt("coarse") + paramInt("fine") / 100.0 + bend);
}

void AsidProcessor::applyControlChanges(int voice, bool forceAll) {
    if (voice != sent.voice) { forceAll = true; sent.voice = voice; }
    if (!forceAll) {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        if (nowMs - lastControlMs < kControlIntervalMs) return;  // coalesce rapid knob drags
        lastControlMs = nowMs;
    }
    auto flush = [&](const Bytes& f) {
        if (!f.empty()) { sendAsid(f); sendAsid(f); }  // send twice to apply
    };

    // The wavetable owns the waveform register while it runs; leave it alone then.
    const int wave = waveBits();
    if (!wtOwnsWave && (forceAll || wave != sent.wave)) {
        sent.wave = wave;
        flush(asidPlayer.setWaveform(voice, static_cast<Byte>(wave)));
    }
    const int s = paramInt("sustain"), rel = paramInt("release");
    const int a = paramInt("attack");
    // At full sustain the decay is inaudible (it "decays" from the peak to the
    // peak), but its rate still drives the SID ADSR counter bug and drops fast
    // retriggers. Send decay 0 there. The knob is disabled and shows 0 to match.
    const int d = (s == 15) ? 0 : paramInt("decay");
    if (forceAll || a != sent.attack || d != sent.decay) {
        sent.attack = a; sent.decay = d;
        flush(asidPlayer.setAttackDecay(voice, a, d));
    }
    if (forceAll || s != sent.sustain || rel != sent.release) {
        sent.sustain = s; sent.release = rel;
        flush(asidPlayer.setSustainRelease(voice, s, rel));
    }
    const int pw = paramInt("pulseWidth");
    // Skip the static pulse width while the LFO or the wavetable drives it.
    if (!lfoOwnedPw && !wtOwnsWave && (forceAll || pw != sent.pw)) { sent.pw = pw; flush(asidPlayer.setPulseWidth(voice, pw)); }
    const int coarse = paramInt("coarse"), fine = paramInt("fine"), bendRange = paramInt("pitchBendRange");
    if (forceAll || coarse != sent.coarse || fine != sent.fine || bendRange != sent.bendRange) {
        sent.coarse = coarse; sent.fine = fine; sent.bendRange = bendRange;
        updatePitchOffset();  // coarse + fine + current pitch-wheel bend
        flush(asidPlayer.setPitchMod(voice, 0.0));  // retune a held note (empty if none)
    }
    // Skip the static sync/ring while the wavetable drives them per step.
    const int sync = paramInt("sync");
    if (!wtOwnsWave && (forceAll || sync != sent.sync)) { sent.sync = sync; flush(asidPlayer.setSync(voice, sync != 0)); }
    const int ring = paramInt("ring");
    if (!wtOwnsWave && (forceAll || ring != sent.ring)) { sent.ring = ring; flush(asidPlayer.setRing(voice, ring != 0)); }
    // TEST bit holds the oscillator in reset while on. The wavetable can drive it
    // per step, so skip the static write while the table owns the voice.
    const int test = paramInt("test");
    if (!wtOwnsWave && (forceAll || test != sent.test)) { sent.test = test; flush(asidPlayer.setTest(voice, test != 0)); }
    // Filter routing (3 shared voice bits) and resonance both live in register
    // 0x17. Routing and resonance are shared, so only the instance where the
    // value actually changed sends it (a synced-in echo is skipped).
    const int routing = routingMask();
    const int res = paramInt("resonance");
    bool reg17dirty = false;
    // echo*.exchange(-1) consumes the one-shot echo: a synced-in value is skipped
    // once, but a later user edit to that same value is not wrongly suppressed.
    if (forceAll || routing != sent.routing) {
        sent.routing = routing;
        AsidShared::get().routing.store(routing);
        if (forceAll || routing != echoRouting.exchange(-1)) reg17dirty = true;
    }
    if (forceAll || res != sent.resonance) {
        sent.resonance = res;
        if (forceAll || res != echoResonance.exchange(-1)) reg17dirty = true;
    }
    bool globalSent = false;
    if (reg17dirty) { flush(asidPlayer.setResonanceRouting(res, routing)); globalSent = true; }

    // Shared filter and volume: only the instance that changed the value sends
    // it. The others share the one physical filter, so they stay off the wire.
    const int cutoff = paramInt("cutoff");
    if (forceAll || cutoff != sent.cutoff) {
        sent.cutoff = cutoff;
        // Skip while an LFO is sweeping the shared cutoff, or they fight.
        const bool cutoffFree = !AsidShared::get().cutoffModActive();
        if (cutoffFree && (forceAll || cutoff != echoCutoff.exchange(-1))) { flush(asidPlayer.setCutoff(cutoff)); globalSent = true; }
    }
    const int mode = modeMask();
    if (forceAll || mode != sent.mode) {
        sent.mode = mode;
        // Translate the logical LP/BP/HP mask to the SID mode bits (combinable).
        const Byte modeBits = static_cast<Byte>((mode & 1 ? sid::kLowPass : 0)
                                              | (mode & 2 ? sid::kBandPass : 0)
                                              | (mode & 4 ? sid::kHighPass : 0)
                                              | (mode & 8 ? sid::kVoice3Off : 0));
        if (forceAll || mode != echoMode.exchange(-1)) { flush(asidPlayer.setFilterMode(modeBits)); globalSent = true; }
    }
    const int vol = paramInt("volume");
    if (forceAll || vol != sent.volume) {
        sent.volume = vol;
        if (forceAll || vol != echoVolume.exchange(-1)) { flush(asidPlayer.setVolume(vol)); globalSent = true; }
    }
    // The shared filter/volume registers are not part of the per-tick modulation
    // stream, so a change made while this voice is idle has no following frame to
    // commit it on the one-message-late unit (it "sticks" until the next edit). A
    // settle frame (a harmless re-write) pushes the pending register through now.
    if (globalSent && asidPlayer.currentNoteOf(voice) < 0)
        flush(asidPlayer.settleFrame(voice));
}

double AsidProcessor::sampleLfo(sidstation::Lfo& lfo, const juce::String& prefix, double dt,
                               bool playing, double ppq, double bpm) {
    lfo.setShape(static_cast<sidstation::LfoShape>(juce::jlimit(0, 6, paramInt(prefix + "Shape"))));
    if (paramInt(prefix + "Sync") != 0) {
        const double beats = beatsForDivision(paramInt(prefix + "Div"));
        if (playing) lfo.setPhase(ppq / beats);          // locked to the song
        else lfo.advance(dt, (bpm / 60.0) / beats);      // free-run at the synced rate when stopped
    } else {
        lfo.advance(dt, static_cast<double>(paramFloat(prefix + "Rate")));
    }
    return lfo.value();  // bipolar [-1, 1]
}

// Every modulation source runs on one clock and its register changes go out in a
// single combined ASID frame per tick, so the SidStation (which applies ~one ASID
// update per SID frame) never gets more frames than it can use. Running many mods
// with separate frames overran that limit and starved some registers.
void AsidProcessor::updateModulation(int voice, bool blockHasNotes) {
    using sidstation::SidState;
    const int base = SidState::voiceBase(voice);

    bool playing = false, hostHasBpm = false;
    double ppq = 0.0, bpm = 120.0, blockPlayheadMs = 0.0;
    if (auto* ph = getPlayHead()) {
        if (const auto pos = ph->getPosition()) {
            playing = pos->getIsPlaying();
            if (const auto q = pos->getPpqPosition()) ppq = *q;
            if (const auto b = pos->getBpm(); b && *b > 0.0) { bpm = *b; hostHasBpm = true; }
            const double srr = juce::jmax(1.0, getSampleRate());
            if (const auto s = pos->getTimeInSamples()) blockPlayheadMs = *s * 1000.0 / srr;
            else if (const auto t = pos->getTimeInSeconds()) blockPlayheadMs = *t * 1000.0;
        }
    }
    // Standalone (or a host that reports no tempo): lock the synced LFOs to the user
    // BPM param. Expose the host BPM to the editor: 0 means "none", so it shows an
    // editable field; a positive value is the fixed host tempo it displays read-only.
    const bool useParamBpm = wrapperType == wrapperType_Standalone || !hostHasBpm;
    if (useParamBpm) bpm = static_cast<double>(paramInt("bpm"));
    hostBpmValue.store(useParamBpm ? 0.0 : bpm, std::memory_order_relaxed);
    // On the block where the transport just stopped, scheduleNotes releases the held
    // note. Streaming this voice's control register here would carry the still-high
    // gate and, scheduled at now+latency, land after that release and re-gate the
    // voice (with two voices the per-instance timing differs, so one hangs). Skip it.
    const bool modStopTransition = !playing && lastModPlaying;
    lastModPlaying = playing ? 1 : 0;
    // Modulation plays on the same aligned timeline as the notes, so on an
    // ahead-rendered track the pitch/PW stream never runs ahead of the note it
    // belongs to. Stopped: now. Playing: the block's playhead mapped to wall time.
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    double sendTimeMs = nowMs + static_cast<double>(paramInt("latency"));
    if (playing) {
        const double aligned = blockPlayheadMs - AsidShared::get().playOffset()
                             + static_cast<double>(paramInt("latency"));
        const double ahead = aligned - nowMs;
        if (ahead >= -50.0 && ahead <= kMaxScheduleAheadMs) sendTimeMs = aligned;
    }

    const int curNote = asidPlayer.currentNoteOf(voice);

    // Ownership and active-state bookkeeping (every block, so hand-offs are prompt).
    const bool wtOn = paramInt("wtOn") != 0;
    const bool wtActive = wtOn && curNote >= 0;
    if (wtActive) wtPlayer.configure(paramInt("wtLength"), paramInt("wtLoop"), paramInt("wtSpeed"));
    else {
        wtPlayer.stop();
        wtArp = 0;
        wtStepDisplay.store(-1, std::memory_order_relaxed);  // nothing playing
    }
    // Hand the waveform / sync / ring / pulse-width registers back to the static
    // controls when the wavetable is switched off (its on->off edge), even with no
    // note playing, so the SID does not stay stuck on the last table waveform.
    // We do this on the switch-off edge, not merely on a note release: on release
    // the envelope is still sounding, so the table's last values play through it.
    // sent.*=-1 forces applyControlChanges to resend the oscillator settings.
    if (lastWtOn && !wtOn) { sent.wave = sent.sync = sent.ring = sent.pw = sent.test = -1; }
    lastWtOn = wtOn;
    wtOwnsWave = wtActive;

    const int pwDepth = paramInt("pwLfoDepth");
    // PW mod only matters when the pulse waveform actually sounds (noise would
    // suppress it), and the wavetable takes over pulse width while it plays.
    const bool pwOn = paramInt("pwLfoOn") && pwDepth > 0
                      && paramInt("wavePulse") && !paramInt("waveNoise") && !wtActive;
    if (!pwOn && lfoOwnedPw) sent.pw = -1;
    lfoOwnedPw = pwOn;

    const int cutDepth = paramInt("cutLfoDepth");
    // Only modulate the shared cutoff when at least one voice is actually routed
    // through the filter. With no voice filtered the cutoff changes nothing, so the
    // stream would be wasted MIDI bandwidth. Routing is shared, so any routed voice
    // (not just this instance's) counts.
    const bool filterInUse = routingMask() != 0;
    if (paramInt("cutLfoOn") && cutDepth > 0 && filterInUse) AsidShared::get().claimCutoffMod(this);
    else AsidShared::get().releaseCutoffMod(this);
    const bool cutOn = cutDepth > 0 && filterInUse && AsidShared::get().isCutoffModOwner(this);
    if (!cutOn && lfoOwnedCutoff) sent.cutoff = -1;
    lfoOwnedCutoff = cutOn;

    const int portaTimeMs = paramInt("portaTime");
    const int pitchDepth = paramInt("pitchLfoDepth");
    const bool vibratoOn = paramInt("pitchLfoOn") && pitchDepth > 0;

    bool gliding = false;
    if (curNote >= 0) {
        if (glidePitch < 0.0) glidePitch = curNote;
        gliding = portaTimeMs > 0 && std::abs(glidePitch - curNote) > 0.01;
    }
    // Stream this voice's frequency every tick whenever a note is sounding, even
    // with no modulation. A quiet voice needs the steady flow both so a busy voice
    // can't starve it and, as testing showed, so a glide plays correctly (it
    // depends on the continuous stream through its held portions, not just the
    // sliding part). Dropping this to modulating-only broke glide playback.
    const bool pitchOn = curNote >= 0;
    const bool anyMod = pitchOn || pwOn || cutOn || wtActive;

    // A frequency/pulse write must not land on a note-event block (that collision
    // is the stuck note). Idle when nothing modulates, zeroing the clock so the
    // next active tick starts with a fresh dt (else a resumed glide leaps).
    const double interval = modIntervalForRate(paramInt("modRate"));
    if (blockHasNotes || !anyMod || modStopTransition) { modTickMs = 0.0; return; }
    if (modTickMs > 0.0 && nowMs - modTickMs < interval) return;
    double dt = (modTickMs <= 0.0) ? interval : juce::jmin(nowMs - modTickMs, 4.0 * interval);
    dt /= 1000.0;
    modTickMs = nowMs;

    const auto& sid = asidPlayer.state();
    std::vector<sidstation::SidWrite> writes;
    auto addReg = [&](int reg) { writes.push_back({static_cast<sidstation::Byte>(reg), sid.reg[reg]}); };

    // Wavetable: apply the current step's waveform, then advance for the next tick
    // (wtSpeed counts ticks). Its arpeggio folds into the pitch below.
    if (wtActive) {
        if (const int step = wtPlayer.currentStep(); step >= 0) {
            const juce::String ss(step);
            // Waveform + Sync + Ring share the control register; Pulse Width has its
            // own. The wavetable drives all of them per step while it plays.
            asidPlayer.setWaveform(voice, static_cast<sidstation::Byte>(wtStepWaveBits(step)));
            asidPlayer.setSync(voice, paramInt("wtSync" + ss) != 0);
            asidPlayer.setRing(voice, paramInt("wtRing" + ss) != 0);
            asidPlayer.setTest(voice, paramInt("wtTest" + ss) != 0);
            asidPlayer.setPulseWidth(voice, paramInt("wtPw" + ss));
            addReg(base + 2);
            addReg(base + 3);
            wtArp = paramInt("wtArp" + ss);
            wtStepDisplay.store(step, std::memory_order_relaxed);  // for the editor
        }
        wtPlayer.advanceFrame();
    }

    // Pitch: portamento glide + vibrato + wavetable arpeggio, one frequency value.
    if (pitchOn) {
        if (gliding) {
            const double step = (12.0 / (portaTimeMs / 1000.0)) * dt;  // ms per octave
            if (glidePitch < curNote) glidePitch = std::min(static_cast<double>(curNote), glidePitch + step);
            else glidePitch = std::max(static_cast<double>(curNote), glidePitch - step);
        }
        const double heard = (paramInt("portaType") == 1) ? std::round(glidePitch) : glidePitch;  // stepped glide
        const double vibrato = vibratoOn
            ? sampleLfo(pitchStream.lfo, "pitchLfo", dt, playing, ppq, bpm) * lfoAmount("pitchLfo", nowMs) * 12.0
            : 0.0;
        asidPlayer.setPitchMod(voice, (heard - curNote) + vibrato + wtArp);
        addReg(base + 0);
        addReg(base + 1);
    }

    if (pwOn) {
        const double v = sampleLfo(pwStream.lfo, "pwLfo", dt, playing, ppq, bpm);
        asidPlayer.setPulseWidth(voice, juce::jlimit(0, 4095,
            paramInt("pulseWidth") + static_cast<int>(v * lfoAmount("pwLfo", nowMs) * 2047.0)));
        addReg(base + 2);
        addReg(base + 3);
    }

    if (cutOn) {
        const double v = sampleLfo(cutStream.lfo, "cutLfo", dt, playing, ppq, bpm);
        asidPlayer.setCutoff(juce::jlimit(0, 2047,
            paramInt("cutoff") + static_cast<int>(v * lfoAmount("cutLfo", nowMs) * 2047.0)));
        addReg(21);  // cutoff low/high (registers 21, 22)
        addReg(22);
    }

    if (writes.empty()) return;
    // This voice's control register goes last (highest slot), so on the
    // one-message-late unit it is the deferred write and everything before it
    // (this voice's freq/pw) flushes inside the frame. It also carries the
    // wavetable waveform when active. One frame per voice, each stamped with that
    // voice's own aligned time, so a voice rendered ahead is not desynced (a
    // single combined frame can only carry one time, which brings the double
    // trigger back when Logic runs voices at different times).
    addReg(base + 4);
    sendAsid(sidstation::encodeAsidUpdate(writes), sendTimeMs);
}

static const char* kSharedIds[] = {"cutoff", "resonance", "volume", "latency", "modRate",
                                   "filt1", "filt2", "filt3", "filtExt",
                                   "modeLP", "modeBP", "modeHP", "voice3off"};

AsidProcessor::AsidProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ASID", makeLayout()) {
    for (const char* id : kSharedIds) apvts.addParameterListener(id, this);
    // Track this instance's voice so the editor can mark/block voices already in use.
    apvts.addParameterListener("asidVoice", this);

    AsidShared::get().addClient(this);
    // Match the filter and volume of instances that are already open.
    if (AsidShared::get().hasData.load()) sharedUpdated();
    AsidShared::get().setClientVoice(this, paramInt("asidVoice"));
}

AsidProcessor::~AsidProcessor() {
    // Filter routing is now shared state (filt1/2/3), not owned by this instance,
    // so leave it for the others; only drop this instance's cutoff-mod claim.
    AsidShared::get().releaseCutoffMod(this);
    AsidShared::get().removeClient(this);
    for (const char* id : kSharedIds) apvts.removeParameterListener(id, this);
    apvts.removeParameterListener("asidVoice", this);
}

void AsidProcessor::parameterChanged(const juce::String& id, float value) {
    // Track which SID voice this instance drives, so the editor can mark and block
    // voices already taken by another instance.
    if (id == "asidVoice") {
        AsidShared::get().setClientVoice(this, juce::jlimit(0, 2, juce::roundToInt(value)));
        return;
    }
    if (!AsidShared::isShared(id)) return;
    auto& sh = AsidShared::get();
    // Ignore an echo of a value we just synced in from another instance.
    if (static_cast<int>(std::lround(value)) == sh.valueFor(id)) return;
    // The user changed a shared control here: publish it to the other instances.
    sh.publish(paramInt("cutoff"), paramInt("resonance"), modeMask(), routingMask(),
               paramInt("volume"), paramInt("latency"), paramInt("modRate"), this);
}

bool AsidProcessor::voiceUsedByOthers(int v) const {
    return AsidShared::get().usersOnVoice(v, this) > 0;
}

void AsidProcessor::resetVoiceToDefault() {
    for (auto* p : getParameters())
        if (auto* wid = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            if (isVoiceSoundParam(wid->paramID) && apvts.getParameter(wid->paramID) != nullptr)
                p->setValueNotifyingHost(p->getDefaultValue());
}

juce::File AsidProcessor::presetsDir() const {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("DehliMusikk").getChildFile("SidStation ASID").getChildFile("Presets");
    if (!dir.isDirectory()) dir.createDirectory();
    return dir;
}

juce::StringArray AsidProcessor::presetNames() const {
    juce::StringArray names;
    for (const auto& f : presetsDir().findChildFiles(juce::File::findFiles, false, "*.xml"))
        names.add(f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

void AsidProcessor::savePreset(const juce::String& name) {
    const auto clean = name.trim();
    if (clean.isEmpty()) return;
    juce::ValueTree tree("SidStationAsidPreset");
    for (auto* p : getParameters())
        if (auto* wid = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            if (isVoiceSoundParam(wid->paramID) && apvts.getParameter(wid->paramID) != nullptr)
                tree.setProperty(wid->paramID, paramFloat(wid->paramID), nullptr);  // raw value
    if (auto xml = tree.createXml())
        xml->writeTo(presetsDir().getChildFile(clean + ".xml"));
    currentPresetName = clean;
}

bool AsidProcessor::loadPreset(const juce::String& name) {
    const auto file = presetsDir().getChildFile(name.trim() + ".xml");
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr) return false;
    const auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid()) return false;
    resetVoiceToDefault();  // params absent from an older preset fall back to default
    for (int i = 0; i < tree.getNumProperties(); ++i) {
        const auto id = tree.getPropertyName(i).toString();
        if (!isVoiceSoundParam(id)) continue;
        if (auto* p = apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1((float) tree.getProperty(id)));
    }
    currentPresetName = name.trim();
    return true;
}

void AsidProcessor::deletePreset(const juce::String& name) {
    const auto file = presetsDir().getChildFile(name.trim() + ".xml");
    if (file.existsAsFile()) file.moveToTrash();  // recoverable
    if (currentPresetName == name.trim()) currentPresetName.clear();
}


void AsidProcessor::sharedUpdated() {
    auto& sh = AsidShared::get();
    // Set the param and remember it as an echo, so applyControlChanges knows this
    // value came from another instance and does not re-send it to the hardware.
    setParamValue("cutoff", sh.cutoff.load());       echoCutoff.store(sh.cutoff.load());
    setParamValue("resonance", sh.resonance.load()); echoResonance.store(sh.resonance.load());
    setParamValue("volume", sh.volume.load());       echoVolume.store(sh.volume.load());
    setParamValue("latency", sh.latency.load());
    setParamValue("modRate", sh.modRate.load());  // shared modulation clock, no hardware echo
    const int r = sh.routing.load();
    setParamValue("filt1", (r >> 0) & 1);
    setParamValue("filt2", (r >> 1) & 1);
    setParamValue("filt3", (r >> 2) & 1);
    setParamValue("filtExt", (r >> 3) & 1);
    echoRouting.store(r);
    const int md = sh.mode.load();
    setParamValue("modeLP", (md >> 0) & 1);
    setParamValue("modeBP", (md >> 1) & 1);
    setParamValue("modeHP", (md >> 2) & 1);
    setParamValue("voice3off", (md >> 3) & 1);
    echoMode.store(md);
}

void AsidProcessor::setParamValue(const char* id, int value) {
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(value)));
}

bool AsidProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void AsidProcessor::sendAsid(const Bytes& asidMessage, double sendTimeMs) {
    if (asidMessage.size() < 2) return;
    const double t = (sendTimeMs < 0.0) ? juce::Time::getMillisecondCounterHiRes() : sendTimeMs;
    AsidShared::get().addBytes(static_cast<int>(asidMessage.size()));  // for the load meter
    // Route through the same timed background sender as the notes (at "now"), so
    // control and modulation frames stay ordered with the note frames on the one
    // port. Sending some frames immediately from the audio thread while notes go
    // out on the background thread races them, which scrambles the one-message-
    // late flush and, for pitch (shared frequency register), stuck/lost notes.
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::createSysExMessage(
                     asidMessage.data() + 1, static_cast<int>(asidMessage.size()) - 2),
                 0);
    midi().sendScheduled(buf, t, 1000.0);
}

void AsidProcessor::addFrame(juce::MidiBuffer& out, const Bytes& frame, int samplePos) {
    if (frame.size() < 2) return;
    AsidShared::get().addBytes(static_cast<int>(frame.size()));  // for the load meter
    out.addEvent(juce::MidiMessage::createSysExMessage(
                     frame.data() + 1, static_cast<int>(frame.size()) - 2),
                 samplePos);
}

void AsidProcessor::scheduleNotes(const juce::MidiBuffer& midiMessages, int voice) {
    const double sr = juce::jmax(1.0, getSampleRate());
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double latencyMs = static_cast<double>(paramInt("latency"));

    // While the transport plays, align to a shared reference so every instance
    // and the DAW agree, even though Logic renders tracks at different times.
    bool playing = false;
    double blockPlayheadMs = 0.0;
    if (auto* ph = getPlayHead()) {
        if (const auto pos = ph->getPosition()) {
            playing = pos->getIsPlaying();
            if (const auto s = pos->getTimeInSamples()) blockPlayheadMs = *s * 1000.0 / sr;
            else if (const auto t = pos->getTimeInSeconds()) blockPlayheadMs = *t * 1000.0;
        }
    }
    // Reset the shared reference on transport start or a big forward jump. A
    // loop jumps the playhead backward, which lowers the offset, and the running
    // minimum follows that down on its own, so looping stays smooth without a
    // reset (which would cause a brief scramble before it re-locks).
    if (playing && (!lastPlaying || blockPlayheadMs > lastPlayheadMs + 500.0))
        AsidShared::get().resetPlayReference();
    const bool justStopped = !playing && lastPlaying;  // transport stopped or paused
    lastPlaying = playing ? 1 : 0;
    lastPlayheadMs = blockPlayheadMs;

    if (playing) AsidShared::get().reportPlayOffset(blockPlayheadMs - nowMs);
    const double refOffset = AsidShared::get().playOffset();

    // Frames are stamped as millisecond offsets from nowMs (sampleRate 1000, so
    // one "sample" is one ms), then handed to the timed background sender.
    juce::MidiBuffer out;

    // The host does not send a note-off for a note still sounding when the
    // transport stops or pauses, so release this voice now to avoid a stuck note.
    // Like any note-off, the gate-off needs a message behind it to take effect on
    // the one-message-late unit; the stream has stopped, so add a settle frame.
    if (justStopped) {
        // Release EVERY voice, not just ours. A note usually hangs on another
        // instance whose processBlock the host stopped calling, so it never runs its
        // own release; whichever instance still gets this stop block releases them
        // all (deduped, so overlapping stop-panics do not overrun the unit).
        // releaseGen makes each instance clear its stale note on resume.
        AsidShared::get().panicOnStop();
        glidePitch = -1.0;
    }

    // Every sounding voice streams its frequency, and that stream stops on
    // release, so every note-off's gate-low needs a settle frame behind it.
    const bool pitchActive = true;

    for (const auto meta : midiMessages) {
        const auto m = meta.getMessage();
        const bool on = m.isNoteOn();
        const bool off = m.isNoteOff();
        if (!on && !off) continue;

        const int ch = m.getChannel() - 1;
        const double sampleOffsetMs = meta.samplePosition * 1000.0 / sr;
        const double immediateMs = nowMs + sampleOffsetMs + latencyMs;
        // Playing: place the note at its song position mapped through the shared
        // reference. At a loop boundary instances briefly straddle two iterations
        // and the reference can jump by a whole loop, so if the aligned time is
        // implausibly far off, fall back to now rather than bursting notes into
        // the future. Stopped or live: always now. Latency trim applies to both.
        double eventMs = immediateMs;
        if (playing) {
            const double aligned = (blockPlayheadMs + sampleOffsetMs) - refOffset + latencyMs;
            const double ahead = aligned - nowMs;
            if (ahead >= -50.0 && ahead <= kMaxScheduleAheadMs) eventMs = aligned;
        }

        // Portamento decision, made BEFORE the note-on so the note-on frame itself
        // gates on at the glide start pitch. wasHeld (a note already sounding =
        // overlap) is the legato test and must be read before noteOn retargets.
        bool freshAttack = false;
        if (on) {
            const bool wasHeld = asidPlayer.currentNoteOf(voice) >= 0;
            freshAttack = !wasHeld;  // nothing sounding = a real attack, not a legato overlap
            if (freshAttack) noteOnMs = nowMs;  // restart the LFO fade-in on a fresh attack
            const bool always = paramInt("portaTrigger") == 1;  // 0 Legato, 1 Always
            const bool glide = paramInt("portaTime") > 0 && glidePitch >= 0.0 && (always || wasHeld);
            if (glide) asidPlayer.setNextGlideStart(glidePitch);  // start at the held pitch
            else glidePitch = m.getNoteNumber();                  // jump straight to the note

            wtPlayer.trigger();  // restart the wavetable from step 0 on a fresh attack
            // Gate in on the table's first-step waveform, not the static one, so
            // there is no burst of the plain oscillator before the modulation
            // clock's first tick swaps the waveform in (that gap, and so the
            // burst, grows as the Mod Rate falls).
            if (paramInt("wtOn"))
                asidPlayer.setWaveform(voice, static_cast<sidstation::Byte>(wtStepWaveBits(0)));
        }

        double target = juce::jmax(eventMs, voiceClockMs);

        // Hard restart: a fresh attack landing while the previous note is still
        // releasing hits the 6581 ADSR bug (a high release rate counter stalls the
        // attack into silence). Drain the envelope with two frames just ahead of
        // the note (frame two flushes frame one into effect on the one-message-late
        // unit); the note-on then restores the real release. This pushes the attack
        // back by the drain window, and only kicks in when a recent release is
        // still ringing, so ordinary notes are untouched.
        if (freshAttack) {
            const int rel = paramInt("release");
            if (rel > 0 && (target - lastGateOffMs) < sidReleaseMs(rel)) {
                const auto hr = asidPlayer.hardRestartFrames(voice);
                if (hr.size() == 2) {
                    const double flushAt = target + kHardRestartMs * 0.5;
                    addFrame(out, hr[0], juce::jmax(0, juce::roundToInt(target - nowMs)));
                    addFrame(out, hr[1], juce::jmax(0, juce::roundToInt(flushAt - nowMs)));
                    target = flushAt + kHardRestartMs * 0.5;  // attack once the envelope has drained
                    voiceClockMs = target;
                }
            }
        }

        const auto frames = on ? asidPlayer.noteOn(ch, m.getNoteNumber(), m.getVelocity())
                               : asidPlayer.noteOff(ch, m.getNoteNumber());
        const int posMs = juce::jmax(0, juce::roundToInt(target - nowMs));
        for (const auto& f : frames) addFrame(out, f, posMs);
        voiceClockMs = target;

        if (off) {
            // Releasing the top note may fall back to a still-held lower note (a
            // hammer-off). The engine retunes the voice, but the pitch stream still
            // drives the frequency from glidePitch, so it must follow. With glide
            // off, jump glidePitch to the held note, else the stream keeps sounding
            // the released note's pitch. With glide on, leave glidePitch where it is
            // so the stream slides back down to the held note on its own.
            const int fellBackTo = asidPlayer.currentNoteOf(voice);
            if (fellBackTo >= 0) {
                if (paramInt("portaTime") == 0) glidePitch = fellBackTo;
            } else {
                lastGateOffMs = target;  // fully released; times the next attack's hard restart
            }
            // A note-off's gate-low needs a message behind it; under pitch mod the
            // stream has stopped, so add a benign settle frame just after.
            if (fellBackTo < 0 && pitchActive) {
                const double settleTarget = target + kSettleMs;
                addFrame(out, asidPlayer.settleFrame(voice), juce::jmax(0, juce::roundToInt(settleTarget - nowMs)));
                voiceClockMs = settleTarget;
            }
        }
    }

    if (!out.isEmpty()) midi().sendScheduled(out, nowMs, 1000.0);
}

void AsidProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                 juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    // This instance drives one SID voice. Set it before any note handling.
    const int voice = paramInt("asidVoice");
    asidPlayer.setTargetVoice(voice);

    // If the shared watchdog released our voice while our processBlock was stalled
    // (the host stopped calling it), clear the stale note here on resume so we do
    // not re-gate it before the transport's own notes play.
    if (const int rg = AsidShared::get().releaseGen(voice); rg != lastReleaseGen) {
        lastReleaseGen = rg;
        if (asidPlayer.currentNoteOf(voice) >= 0) { asidPlayer.allNotesOff(); glidePitch = -1.0; }
    }

    // Any instance opening the shared device bumps the generation; re-push then.
    if (const int gen = AsidShared::get().outGeneration.load(); gen != lastOutGeneration) {
        lastOutGeneration = gen;
        initRequest.store(true);
    }

    // On first block or after a device (re)open, push the full state.
    bool forceControls = false;
    if (initRequest.exchange(false)) {
        asidPlayer.reset();
        for (const auto& msg : asidPlayer.start()) { sendAsid(msg); sendAsid(msg); }
        forceControls = true;  // push the current control values after the start state
    }

    // Controls go out immediately (not rhythmic, and this sets the filter before
    // any note that depends on it plays).
    applyControlChanges(voice, forceControls);

    bool blockHasNotes = false;
    for (const auto meta : midiMessages) {
        const auto m = meta.getMessage();
        if (m.isNoteOn() || m.isNoteOff()) blockHasNotes = true;
        else if (m.isPitchWheel()) { pitchWheelValue = m.getPitchWheelValue(); updatePitchOffset(); }
        else if (m.isController() && m.getControllerNumber() == 1) modWheelValue = m.getControllerValue();
    }
    updateModulation(voice, blockHasNotes);

    scheduleNotes(midiMessages, voice);

    // Heartbeat for the shared stuck-note watchdog: report our voice's held note (or
    // -1). If we stop reporting (the host stops calling processBlock) while a note is
    // held, the watchdog thread releases it - the one context that stays alive when a
    // track is unselected and the transport stops.
    AsidShared::get().reportVoiceNote(voice, asidPlayer.currentNoteOf(voice),
                                      juce::Time::getMillisecondCounterHiRes());

    buffer.clear();  // sound comes from the hardware
}

juce::AudioProcessorEditor* AsidProcessor::createEditor() { return new AsidEditor(*this); }

void AsidProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    state.setProperty("currentPreset", currentPresetName, nullptr);  // for the editor's display
    state.setProperty("showTooltips", tooltipsOn, nullptr);          // editor "Tips" toggle
    // The MIDI output is shared by all instances, so every instance stores the same
    // device; the name is a fallback for when an identifier changes between sessions.
    state.setProperty("midiOut", midi().outputIdentifier(), nullptr);
    state.setProperty("midiOutName", midi().outputName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void AsidProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto tree = juce::ValueTree::fromXml(*xml);
        currentPresetName = tree.getProperty("currentPreset", "").toString();
        tooltipsOn = (bool) tree.getProperty("showTooltips", true);
        apvts.replaceState(tree);

        // Reopen the saved MIDI output unless the shared hub already has it open
        // (another instance restored it first). Try the identifier, then the name.
        const auto outId = tree.getProperty("midiOut", "").toString();
        const auto outName = tree.getProperty("midiOutName", "").toString();
        if (outId.isNotEmpty() && midi().outputIdentifier() != outId) {
            const bool ok = midi().openOutputByIdentifier(outId)
                         || (outName.isNotEmpty() && midi().openOutputMatching(outName));
            if (ok) AsidShared::get().outGeneration.fetch_add(1);  // make instances re-push
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AsidProcessor(); }
