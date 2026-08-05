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
        case 3: return 10.0;             // Smooth 100 Hz
    }
    return 20.0;
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

    // Per-voice sound. Defaults match the player's default sawtooth voice.
    layout.add(std::make_unique<Choice>(juce::ParameterID{"waveform", 1}, "Waveform",
                                        juce::StringArray{"Triangle", "Sawtooth", "Pulse", "Noise"}, 1));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("attack", "Attack", 0, 15, 0)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("decay", "Decay", 0, 15, 0)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("sustain", "Sustain", 0, 15, 15)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("release", "Release", 0, 15, 0)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("pulseWidth", "Pulse Width", 0, 4095, 2048)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("coarse", "Coarse Tune", -24, 24, 0)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("fine", "Fine Tune", -50, 50, 0)));
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

    // Shared across all three voices (one physical SID filter and master volume).
    // Filter routing is per voice (filt1/2/3) but shared, so any instance can
    // route any voice. Filter mode bits (LP/BP/HP) combine, as the SID allows.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"filt1", 1}, "Filter Voice 1", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"filt2", 1}, "Filter Voice 2", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"filt3", 1}, "Filter Voice 3", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"modeLP", 1}, "Filter Low Pass", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"modeBP", 1}, "Filter Band Pass", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"modeHP", 1}, "Filter High Pass", false));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("cutoff", "Cutoff", 0, 2047, 2047)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("resonance", "Resonance", 0, 15, 0)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("volume", "Volume", 0, 15, 15)));
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
    };
    addLfo("pitchLfo", "Pitch LFO");
    addLfo("pwLfo", "PW LFO");
    addLfo("cutLfo", "Cutoff LFO");

    // One modulation clock for the whole voice: every source (LFOs, glide,
    // wavetable) is sampled on this tick and sent in a single combined ASID
    // frame, so the frame rate stays within what the SidStation applies per SID
    // frame. PAL 50 Hz is the native SID rate; Smooth over-sends and can drop.
    layout.add(std::make_unique<Choice>(juce::ParameterID{"modRate", 1}, "Mod Rate",
        juce::StringArray{"Eco 25 Hz", "PAL 50 Hz", "NTSC 60 Hz", "Smooth 100 Hz"}, 1));

    // Wavetable: a per-voice table stepped once per PAL frame (~50 Hz), the SID
    // "waveform table" done in software. Each of the 8 steps sets a waveform and
    // an arpeggio offset; the table advances every `wtSpeed` frames and loops.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"wtOn", 1}, "Wavetable On", false));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("wtSpeed", "WT Speed", 1, 16, 2)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("wtLength", "WT Length", 1, kWtSteps, 1)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("wtLoop", "WT Loop", 0, kWtSteps - 1, 0)));
    const juce::StringArray wtWaves{"Triangle", "Sawtooth", "Pulse", "Noise",
                                    "Tri+Saw", "Pulse+Tri", "Pulse+Saw", "Silence"};
    for (int i = 0; i < kWtSteps; ++i) {
        layout.add(std::make_unique<Choice>(juce::ParameterID{"wtWave" + juce::String(i), 1},
                                            "WT Wave " + juce::String(i + 1), wtWaves, 0));
        layout.add(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{"wtArp" + juce::String(i), 1}, "WT Arp " + juce::String(i + 1), -24, 24, 0));
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

    static const Byte kWave[4] = {sid::kTriangle, sid::kSaw, sid::kPulse, sid::kNoise};

    // The wavetable owns the waveform register while it runs; leave it alone then.
    const int wave = paramInt("waveform");
    if (!wtOwnsWave && (forceAll || wave != sent.wave)) {
        sent.wave = wave;
        flush(asidPlayer.setWaveform(voice, kWave[juce::jlimit(0, 3, wave)]));
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
    // Skip the static pulse width while the LFO is driving it, or they fight.
    if (!lfoOwnedPw && (forceAll || pw != sent.pw)) { sent.pw = pw; flush(asidPlayer.setPulseWidth(voice, pw)); }
    const int coarse = paramInt("coarse"), fine = paramInt("fine");
    if (forceAll || coarse != sent.coarse || fine != sent.fine) {
        sent.coarse = coarse; sent.fine = fine;
        asidPlayer.setPitchOffset(coarse + fine / 100.0);
        flush(asidPlayer.setPitchMod(voice, 0.0));  // retune a held note (empty if none)
    }
    const int sync = paramInt("sync");
    if (forceAll || sync != sent.sync) { sent.sync = sync; flush(asidPlayer.setSync(voice, sync != 0)); }
    const int ring = paramInt("ring");
    if (forceAll || ring != sent.ring) { sent.ring = ring; flush(asidPlayer.setRing(voice, ring != 0)); }
    // Filter routing (3 shared voice bits) and resonance both live in register
    // 0x17. Routing and resonance are shared, so only the instance where the
    // value actually changed sends it (a synced-in echo is skipped).
    const int routing = routingMask();
    const int res = paramInt("resonance");
    bool reg17dirty = false;
    if (forceAll || routing != sent.routing) {
        sent.routing = routing;
        AsidShared::get().routing.store(routing);
        if (forceAll || routing != echoRouting.load()) reg17dirty = true;
    }
    if (forceAll || res != sent.resonance) {
        sent.resonance = res;
        if (forceAll || res != echoResonance.load()) reg17dirty = true;
    }
    if (reg17dirty)
        flush(asidPlayer.setResonanceRouting(res, routing));

    // Shared filter and volume: only the instance that changed the value sends
    // it. The others share the one physical filter, so they stay off the wire.
    const int cutoff = paramInt("cutoff");
    if (forceAll || cutoff != sent.cutoff) {
        sent.cutoff = cutoff;
        // Skip while an LFO is sweeping the shared cutoff, or they fight.
        const bool cutoffFree = !AsidShared::get().cutoffModActive();
        if (cutoffFree && (forceAll || cutoff != echoCutoff.load())) flush(asidPlayer.setCutoff(cutoff));
    }
    const int mode = modeMask();
    if (forceAll || mode != sent.mode) {
        sent.mode = mode;
        // Translate the logical LP/BP/HP mask to the SID mode bits (combinable).
        const Byte modeBits = static_cast<Byte>((mode & 1 ? sid::kLowPass : 0)
                                              | (mode & 2 ? sid::kBandPass : 0)
                                              | (mode & 4 ? sid::kHighPass : 0));
        if (forceAll || mode != echoMode.load()) flush(asidPlayer.setFilterMode(modeBits));
    }
    const int vol = paramInt("volume");
    if (forceAll || vol != sent.volume) {
        sent.volume = vol;
        if (forceAll || vol != echoVolume.load()) flush(asidPlayer.setVolume(vol));
    }
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

    bool playing = false;
    double ppq = 0.0, bpm = 120.0, blockPlayheadMs = 0.0;
    if (auto* ph = getPlayHead()) {
        if (const auto pos = ph->getPosition()) {
            playing = pos->getIsPlaying();
            if (const auto q = pos->getPpqPosition()) ppq = *q;
            if (const auto b = pos->getBpm()) bpm = *b;
            const double srr = juce::jmax(1.0, getSampleRate());
            if (const auto s = pos->getTimeInSamples()) blockPlayheadMs = *s * 1000.0 / srr;
            else if (const auto t = pos->getTimeInSeconds()) blockPlayheadMs = *t * 1000.0;
        }
    }
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
    const bool wtActive = paramInt("wtOn") != 0 && curNote >= 0;
    if (wtActive) wtPlayer.configure(paramInt("wtLength"), paramInt("wtLoop"), paramInt("wtSpeed"));
    else { if (wtOwnsWave) sent.wave = -1; wtPlayer.stop(); wtArp = 0; }
    wtOwnsWave = wtActive;

    const int pwDepth = paramInt("pwLfoDepth");
    const bool pwOn = paramInt("pwLfoOn") && pwDepth > 0 && paramInt("waveform") == 2;  // 2 == Pulse
    if (!pwOn && lfoOwnedPw) sent.pw = -1;
    lfoOwnedPw = pwOn;

    const int cutDepth = paramInt("cutLfoDepth");
    if (paramInt("cutLfoOn") && cutDepth > 0) AsidShared::get().claimCutoffMod(this);
    else AsidShared::get().releaseCutoffMod(this);
    const bool cutOn = cutDepth > 0 && AsidShared::get().isCutoffModOwner(this);
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
    const bool pitchOn = curNote >= 0 && (gliding || vibratoOn || wtActive);
    const bool anyMod = pitchOn || pwOn || cutOn || wtActive;

    // A frequency/pulse write must not land on a note-event block (that collision
    // is the stuck note). Idle when nothing modulates, zeroing the clock so the
    // next active tick starts with a fresh dt (else a resumed glide leaps).
    const double interval = modIntervalForRate(paramInt("modRate"));
    if (blockHasNotes || !anyMod) { modTickMs = 0.0; return; }
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
            static const juce::uint8 kWtWave[8] = {0x10, 0x20, 0x40, 0x80, 0x30, 0x50, 0x60, 0x00};
            asidPlayer.setWaveform(voice, kWtWave[juce::jlimit(0, 7, paramInt("wtWave" + juce::String(step)))]);
            wtArp = paramInt("wtArp" + juce::String(step));
            addReg(base + 4);  // control register (waveform + gate)
        }
        wtPlayer.advanceFrame();
    }

    // Pitch: portamento glide + vibrato + wavetable arpeggio, one frequency value.
    bool glideLanded = false;
    if (pitchOn) {
        if (gliding) {
            const double step = (12.0 / (portaTimeMs / 1000.0)) * dt;  // ms per octave
            if (glidePitch < curNote) glidePitch = std::min(static_cast<double>(curNote), glidePitch + step);
            else glidePitch = std::max(static_cast<double>(curNote), glidePitch - step);
            glideLanded = std::abs(glidePitch - curNote) < 1.0e-9;
        }
        const double heard = (paramInt("portaType") == 1) ? std::round(glidePitch) : glidePitch;  // stepped glide
        const double vibrato = vibratoOn
            ? sampleLfo(pitchStream.lfo, "pitchLfo", dt, playing, ppq, bpm) * (pitchDepth / 100.0) * 12.0
            : 0.0;
        asidPlayer.setPitchMod(voice, (heard - curNote) + vibrato + wtArp);
        addReg(base + 0);
        addReg(base + 1);
    }

    if (pwOn) {
        const double v = sampleLfo(pwStream.lfo, "pwLfo", dt, playing, ppq, bpm);
        asidPlayer.setPulseWidth(voice, juce::jlimit(0, 4095,
            paramInt("pulseWidth") + static_cast<int>(v * (pwDepth / 100.0) * 2047.0)));
        addReg(base + 2);
        addReg(base + 3);
    }

    if (cutOn) {
        const double v = sampleLfo(cutStream.lfo, "cutLfo", dt, playing, ppq, bpm);
        asidPlayer.setCutoff(juce::jlimit(0, 2047,
            paramInt("cutoff") + static_cast<int>(v * (cutDepth / 100.0) * 2047.0)));
        addReg(21);  // cutoff low/high (registers 21, 22)
        addReg(22);
    }

    if (!writes.empty()) sendAsid(sidstation::encodeAsidUpdate(writes), sendTimeMs);

    // If a glide just reached the target and nothing else keeps the stream alive,
    // push one benign flush so the final frequency lands (the unit is one late).
    if (glideLanded && !vibratoOn && !pwOn && !cutOn && !wtActive)
        sendAsid(asidPlayer.settleFrame(voice), sendTimeMs);
}

static const char* kSharedIds[] = {"cutoff", "resonance", "volume", "latency",
                                   "filt1", "filt2", "filt3", "modeLP", "modeBP", "modeHP"};

AsidProcessor::AsidProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ASID", makeLayout()) {
    for (const char* id : kSharedIds) apvts.addParameterListener(id, this);
    AsidShared::get().addClient(this);
    // Match the filter and volume of instances that are already open.
    if (AsidShared::get().hasData.load()) sharedUpdated();
}

AsidProcessor::~AsidProcessor() {
    // Filter routing is now shared state (filt1/2/3), not owned by this instance,
    // so leave it for the others; only drop this instance's cutoff-mod claim.
    AsidShared::get().releaseCutoffMod(this);
    AsidShared::get().removeClient(this);
    for (const char* id : kSharedIds) apvts.removeParameterListener(id, this);
}

void AsidProcessor::parameterChanged(const juce::String& id, float value) {
    if (!AsidShared::isShared(id)) return;
    auto& sh = AsidShared::get();
    // Ignore an echo of a value we just synced in from another instance.
    if (static_cast<int>(std::lround(value)) == sh.valueFor(id)) return;
    // The user changed a shared control here: publish it to the other instances.
    sh.publish(paramInt("cutoff"), paramInt("resonance"), modeMask(), routingMask(),
               paramInt("volume"), paramInt("latency"), this);
}

void AsidProcessor::sharedUpdated() {
    auto& sh = AsidShared::get();
    // Set the param and remember it as an echo, so applyControlChanges knows this
    // value came from another instance and does not re-send it to the hardware.
    setParamValue("cutoff", sh.cutoff.load());       echoCutoff.store(sh.cutoff.load());
    setParamValue("resonance", sh.resonance.load()); echoResonance.store(sh.resonance.load());
    setParamValue("volume", sh.volume.load());       echoVolume.store(sh.volume.load());
    setParamValue("latency", sh.latency.load());
    const int r = sh.routing.load();
    setParamValue("filt1", (r >> 0) & 1);
    setParamValue("filt2", (r >> 1) & 1);
    setParamValue("filt3", (r >> 2) & 1);
    echoRouting.store(r);
    const int md = sh.mode.load();
    setParamValue("modeLP", (md >> 0) & 1);
    setParamValue("modeBP", (md >> 1) & 1);
    setParamValue("modeHP", (md >> 2) & 1);
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
    midiHub.sendScheduled(buf, t, 1000.0);
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
    lastPlaying = playing ? 1 : 0;
    lastPlayheadMs = blockPlayheadMs;

    if (playing) AsidShared::get().reportPlayOffset(blockPlayheadMs - nowMs);
    const double refOffset = AsidShared::get().playOffset();

    // Frames are stamped as millisecond offsets from nowMs (sampleRate 1000, so
    // one "sample" is one ms), then handed to the timed background sender.
    juce::MidiBuffer out;

    // The pitch/waveform streams run under vibrato, a glide, or a wavetable; each
    // stops on release, so the note-off gate-low needs a settle frame behind it.
    const bool pitchActive = (paramInt("pitchLfoOn") && paramInt("pitchLfoDepth") > 0)
                             || paramInt("portaTime") > 0 || paramInt("wtOn") != 0;

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
        if (on) {
            const bool wasHeld = asidPlayer.currentNoteOf(voice) >= 0;
            const bool always = paramInt("portaTrigger") == 1;  // 0 Legato, 1 Always
            const bool glide = paramInt("portaTime") > 0 && glidePitch >= 0.0 && (always || wasHeld);
            if (glide) asidPlayer.setNextGlideStart(glidePitch);  // start at the held pitch
            else glidePitch = m.getNoteNumber();                  // jump straight to the note

            wtPlayer.trigger();  // restart the wavetable from step 0 on a fresh attack
        }

        const auto frames = on ? asidPlayer.noteOn(ch, m.getNoteNumber(), m.getVelocity())
                               : asidPlayer.noteOff(ch, m.getNoteNumber());
        const double target = juce::jmax(eventMs, voiceClockMs);
        const int posMs = juce::jmax(0, juce::roundToInt(target - nowMs));
        for (const auto& f : frames) addFrame(out, f, posMs);
        voiceClockMs = target;

        // A note-off's gate-low needs a message behind it; under pitch mod the
        // stream has stopped, so add a benign settle frame just after.
        if (off && pitchActive) {
            const double settleTarget = target + kSettleMs;
            addFrame(out, asidPlayer.settleFrame(voice), juce::jmax(0, juce::roundToInt(settleTarget - nowMs)));
            voiceClockMs = settleTarget;
        }
    }

    if (!out.isEmpty()) midiHub.sendScheduled(out, nowMs, 1000.0);
}

void AsidProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                 juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    // This instance drives one SID voice. Set it before any note handling.
    const int voice = paramInt("asidVoice");
    asidPlayer.setTargetVoice(voice);

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
        if (m.isNoteOn() || m.isNoteOff()) { blockHasNotes = true; break; }
    }
    updateModulation(voice, blockHasNotes);

    scheduleNotes(midiMessages, voice);

    buffer.clear();  // sound comes from the hardware
}

juce::AudioProcessorEditor* AsidProcessor::createEditor() { return new AsidEditor(*this); }

void AsidProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void AsidProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AsidProcessor(); }
