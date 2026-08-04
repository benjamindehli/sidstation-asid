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
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"filterRoute", 1}, "Route Through Filter", false));

    // Shared across all three voices (one physical SID filter and master volume).
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("cutoff", "Cutoff", 0, 2047, 2047)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("resonance", "Resonance", 0, 15, 0)));
    layout.add(std::make_unique<Choice>(juce::ParameterID{"filterMode", 1}, "Filter Mode",
                                        juce::StringArray{"Low", "Band", "High"}, 0));
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
        layout.add(std::make_unique<C>(
            juce::ParameterID{prefix + "Update", 1}, name + " Update",
            juce::StringArray{"Eco 25 Hz", "PAL 50 Hz", "NTSC 60 Hz", "Smooth 100 Hz"}, 1));
    };
    addLfo("pitchLfo", "Pitch LFO");
    addLfo("pwLfo", "PW LFO");
    addLfo("cutLfo", "Cutoff LFO");
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
    static const Byte kMode[3] = {sid::kLowPass, sid::kBandPass, sid::kHighPass};

    const int wave = paramInt("waveform");
    if (forceAll || wave != sent.wave) {
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
    // Filter routing is chosen per voice but lives in a register shared by all
    // voices. Set our voice's bit in the shared routing, then write the whole
    // resonance+routing register so we never wipe the other voices' bits.
    // Resonance is shared, so only the instance where it actually changed sends
    // it (a synced-in echo is skipped). Routing bits are per voice: whoever
    // toggled sends. Both live in register 0x17.
    const int route = paramInt("filterRoute");
    const int res = paramInt("resonance");
    bool reg17dirty = false;
    if (forceAll || route != sent.route) {
        sent.route = route;
        AsidShared::get().setRoutingBit(voice, route != 0);
        reg17dirty = true;
    }
    if (forceAll || res != sent.resonance) {
        sent.resonance = res;
        if (forceAll || res != echoResonance.load()) reg17dirty = true;
    }
    if (reg17dirty)
        flush(asidPlayer.setResonanceRouting(res, AsidShared::get().routing.load()));

    // Shared filter and volume: only the instance that changed the value sends
    // it. The others share the one physical filter, so they stay off the wire.
    const int cutoff = paramInt("cutoff");
    if (forceAll || cutoff != sent.cutoff) {
        sent.cutoff = cutoff;
        // Skip while an LFO is sweeping the shared cutoff, or they fight.
        const bool cutoffFree = !AsidShared::get().cutoffModActive();
        if (cutoffFree && (forceAll || cutoff != echoCutoff.load())) flush(asidPlayer.setCutoff(cutoff));
    }
    const int mode = paramInt("filterMode");
    if (forceAll || mode != sent.mode) {
        sent.mode = mode;
        if (forceAll || mode != echoMode.load()) flush(asidPlayer.setFilterMode(kMode[juce::jlimit(0, 2, mode)]));
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

bool AsidProcessor::advanceLfo(ModStream& m, const juce::String& prefix, bool playing,
                              double ppq, double bpm, double& valueOut) {
    const double interval = modIntervalForRate(paramInt(prefix + "Update"));
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (nowMs - m.lastMs < interval) return false;  // stream at the chosen rate
    const double dt = (m.lastMs <= 0.0 ? interval : nowMs - m.lastMs) / 1000.0;
    m.lastMs = nowMs;
    valueOut = sampleLfo(m.lfo, prefix, dt, playing, ppq, bpm);
    return true;
}

void AsidProcessor::updatePitch(int voice, bool blockHasNotes, bool playing, double ppq, double bpm) {
    // Idle the stream when there is nothing to drive it, and zero lastMs so the
    // next tick that DOES stream starts with a fresh, one-interval dt. Without
    // this the glide step (rate * dt) sees the whole idle gap as dt and jumps
    // straight to the target: the exact bug that vibrato, by never idling, hid.
    auto idle = [this]() { pitchStream.lastFrame.clear(); pitchStream.lastMs = 0.0; };

    const int curNote = asidPlayer.currentNoteOf(voice);
    if (curNote < 0) { idle(); return; }         // keep glidePitch so "Always" glides from it
    if (glidePitch < 0.0) glidePitch = curNote;  // note started without a glide claim

    const int portaTimeMs = paramInt("portaTime");
    const bool gliding = portaTimeMs > 0 && std::abs(glidePitch - curNote) > 0.01;
    const int pitchDepth = paramInt("pitchLfoDepth");
    const bool vibratoOn = paramInt("pitchLfoOn") && pitchDepth > 0;

    // A frequency write must not land on a note-event block (that collision is the
    // stuck note), and there is nothing to stream unless glide or vibrato is live.
    if (blockHasNotes || (!gliding && !vibratoOn)) { idle(); return; }

    // Stream at the vibrato LFO's update rate when it runs, else a steady 50 Hz
    // for a glide on its own.
    const double interval = vibratoOn ? modIntervalForRate(paramInt("pitchLfoUpdate")) : 20.0;
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (nowMs - pitchStream.lastMs < interval) return;
    // First tick after idle uses one interval; otherwise the real gap, capped so
    // a scheduling hiccup cannot make one glide step leap.
    double dt = (pitchStream.lastMs <= 0.0) ? interval : (nowMs - pitchStream.lastMs);
    dt = juce::jmin(dt, 4.0 * interval) / 1000.0;
    pitchStream.lastMs = nowMs;

    // Glide the pitch toward the target at a constant rate (portaTime = ms per octave).
    if (gliding) {
        const double step = (12.0 / (portaTimeMs / 1000.0)) * dt;
        if (glidePitch < curNote) glidePitch = std::min(static_cast<double>(curNote), glidePitch + step);
        else glidePitch = std::max(static_cast<double>(curNote), glidePitch - step);
    }
    // Stepped portamento quantises the glide to whole semitones.
    const double heard = (paramInt("portaType") == 1) ? std::round(glidePitch) : glidePitch;

    double vibrato = 0.0;
    if (vibratoOn) vibrato = sampleLfo(pitchStream.lfo, "pitchLfo", dt, playing, ppq, bpm) * (pitchDepth / 100.0) * 12.0;

    // setPitchMod adds to the player's integer note, so offset by the glide delta.
    const auto frame = asidPlayer.setPitchMod(voice, (heard - curNote) + vibrato);
    if (frame.empty()) return;
    // The unit applies each write one message late, so a lone write hangs pending
    // until another follows. While gliding, send every tick even if the frame is
    // unchanged (a slow glide barely moves the 16-bit value per tick): the steady
    // stream keeps each step flushed, the same way vibrato does. Otherwise send
    // only on change.
    if (gliding || frame != pitchStream.lastFrame) {
        pitchStream.lastFrame = frame;
        sendAsid(frame);
        // When a glide reaches the target and no vibrato follows to carry the
        // stream, push one benign flush so the final frequency lands instead of
        // hanging a step short.
        if (gliding && !vibratoOn && glidePitch == static_cast<double>(curNote))
            sendAsid(asidPlayer.settleFrame(voice));
    }
}

void AsidProcessor::updateModulation(int voice, bool blockHasNotes) {
    // Transport, read once for any tempo-synced LFO.
    bool playing = false;
    double ppq = 0.0, bpm = 120.0;
    if (auto* ph = getPlayHead()) {
        if (const auto pos = ph->getPosition()) {
            playing = pos->getIsPlaying();
            if (const auto q = pos->getPpqPosition()) ppq = *q;
            if (const auto b = pos->getBpm()) bpm = *b;
        }
    }

    // Pitch: portamento glide and pitch-LFO vibrato as one frequency stream.
    updatePitch(voice, blockHasNotes, playing, ppq, bpm);

    // Pulse-width LFO (per voice, pulse wave only). It owns the pulse-width
    // register while active so applyControlChanges leaves it alone.
    const int pwDepth = paramInt("pwLfoDepth");
    const bool pwOn = paramInt("pwLfoOn") && pwDepth > 0 && paramInt("waveform") == 2;  // 2 == Pulse
    if (pwOn) {
        double v;
        if (advanceLfo(pwStream, "pwLfo", playing, ppq, bpm, v)) {
            const int pw = juce::jlimit(0, 4095, paramInt("pulseWidth") + static_cast<int>(v * (pwDepth / 100.0) * 2047.0));
            const auto frame = asidPlayer.setPulseWidth(voice, pw);
            if (frame != pwStream.lastFrame) { pwStream.lastFrame = frame; sendAsid(frame); }
        }
    } else {
        if (lfoOwnedPw) sent.pw = -1;  // hand pulse width back to the static control
        pwStream.lastFrame.clear();
    }
    lfoOwnedPw = pwOn;

    // Cutoff LFO (shared filter, one owner at a time).
    const int cutDepth = paramInt("cutLfoDepth");
    if (paramInt("cutLfoOn") && cutDepth > 0) AsidShared::get().claimCutoffMod(this);
    else AsidShared::get().releaseCutoffMod(this);
    const bool cutOn = cutDepth > 0 && AsidShared::get().isCutoffModOwner(this);
    if (cutOn) {
        double v;
        if (advanceLfo(cutStream, "cutLfo", playing, ppq, bpm, v)) {
            const int co = juce::jlimit(0, 2047, paramInt("cutoff") + static_cast<int>(v * (cutDepth / 100.0) * 2047.0));
            const auto frame = asidPlayer.setCutoff(co);
            if (frame != cutStream.lastFrame) { cutStream.lastFrame = frame; sendAsid(frame); }
        }
    } else {
        if (lfoOwnedCutoff) sent.cutoff = -1;
        cutStream.lastFrame.clear();
    }
    lfoOwnedCutoff = cutOn;
}

static const char* kSharedIds[] = {"cutoff", "resonance", "filterMode", "volume", "latency"};

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
    // Drop this voice's routing bit so a removed instance stops filtering.
    AsidShared::get().setRoutingBit(paramInt("asidVoice"), false);
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
    sh.publish(paramInt("cutoff"), paramInt("resonance"), paramInt("filterMode"),
               paramInt("volume"), paramInt("latency"), this);
}

void AsidProcessor::sharedUpdated() {
    auto& sh = AsidShared::get();
    // Set the param and remember it as an echo, so applyControlChanges knows this
    // value came from another instance and does not re-send it to the hardware.
    setParamValue("cutoff", sh.cutoff.load());       echoCutoff.store(sh.cutoff.load());
    setParamValue("resonance", sh.resonance.load()); echoResonance.store(sh.resonance.load());
    setParamValue("filterMode", sh.mode.load());     echoMode.store(sh.mode.load());
    setParamValue("volume", sh.volume.load());       echoVolume.store(sh.volume.load());
    setParamValue("latency", sh.latency.load());
}

void AsidProcessor::setParamValue(const char* id, int value) {
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(value)));
}

bool AsidProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void AsidProcessor::sendAsid(const Bytes& asidMessage) {
    if (asidMessage.size() < 2) return;
    // Route through the same timed background sender as the notes (at "now"), so
    // control and modulation frames stay ordered with the note frames on the one
    // port. Sending some frames immediately from the audio thread while notes go
    // out on the background thread races them, which scrambles the one-message-
    // late flush and, for pitch (shared frequency register), stuck/lost notes.
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::createSysExMessage(
                     asidMessage.data() + 1, static_cast<int>(asidMessage.size()) - 2),
                 0);
    midiHub.sendScheduled(buf, juce::Time::getMillisecondCounterHiRes(), 1000.0);
}

void AsidProcessor::addFrame(juce::MidiBuffer& out, const Bytes& frame, int samplePos) {
    if (frame.size() < 2) return;
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

    // The pitch stream runs under vibrato or a portamento glide; either way it
    // stops on release, so the note-off gate-low needs a settle frame behind it.
    const bool pitchActive = (paramInt("pitchLfoOn") && paramInt("pitchLfoDepth") > 0)
                             || paramInt("portaTime") > 0;

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
