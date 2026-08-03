#include "AsidProcessor.h"

#include <cmath>

#include "AsidEditor.h"

using namespace sidstation;

namespace {
juce::AudioParameterInt* intParam(const char* id, const char* name, int lo, int hi, int def) {
    return new juce::AudioParameterInt(juce::ParameterID{id, 1}, name, lo, hi, def);
}
juce::AudioParameterFloat* floatParam(const char* id, const char* name,
                                      juce::NormalisableRange<float> range, float def) {
    return new juce::AudioParameterFloat(juce::ParameterID{id, 1}, name, range, def);
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

    // Modulation: one plugin-side LFO per voice (the SID has none). Pulse width
    // is the only target for now; the engine is written to add more later.
    layout.add(std::make_unique<Choice>(juce::ParameterID{"lfoTarget", 1}, "LFO Target",
                                        juce::StringArray{"Off", "Pulse Width"}, 0));
    layout.add(std::make_unique<Choice>(
        juce::ParameterID{"lfoShape", 1}, "LFO Shape",
        juce::StringArray{"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random"}, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"lfoSync", 1}, "LFO Sync", false));
    layout.add(std::unique_ptr<juce::AudioParameterFloat>(floatParam(
        "lfoRate", "LFO Rate", juce::NormalisableRange<float>(0.05f, 20.0f, 0.0f, 0.35f), 2.0f)));
    layout.add(std::make_unique<Choice>(
        juce::ParameterID{"lfoDivision", 1}, "LFO Division",
        juce::StringArray{"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T"}, 2));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("lfoDepth", "LFO Depth", 0, 100, 50)));
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
    const int a = paramInt("attack"), d = paramInt("decay");
    if (forceAll || a != sent.attack || d != sent.decay) {
        sent.attack = a; sent.decay = d;
        flush(asidPlayer.setAttackDecay(voice, a, d));
    }
    const int s = paramInt("sustain"), rel = paramInt("release");
    if (forceAll || s != sent.sustain || rel != sent.release) {
        sent.sustain = s; sent.release = rel;
        flush(asidPlayer.setSustainRelease(voice, s, rel));
    }
    const int pw = paramInt("pulseWidth");
    // Skip the static pulse width while the LFO is driving it, or they fight.
    if (!lfoOwnedPw && (forceAll || pw != sent.pw)) { sent.pw = pw; flush(asidPlayer.setPulseWidth(voice, pw)); }
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
        if (forceAll || cutoff != echoCutoff.load()) flush(asidPlayer.setCutoff(cutoff));
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

void AsidProcessor::updateModulation(int voice) {
    const bool active = (paramInt("lfoTarget") == 1) && (paramInt("lfoDepth") > 0);  // 1 == Pulse Width
    if (!active) {
        // Hand pulse width back to the static control on the next block.
        if (lfoOwnedPw) { lfoOwnedPw = false; sent.pw = -1; lastModPw = -1; }
        return;
    }
    lfoOwnedPw = true;

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (nowMs - lastModMs < kModIntervalMs) return;  // hold the stream near 30 Hz
    const double dt = (lastModMs <= 0.0 ? kModIntervalMs : nowMs - lastModMs) / 1000.0;
    lastModMs = nowMs;

    lfo.setShape(static_cast<sidstation::LfoShape>(juce::jlimit(0, 5, paramInt("lfoShape"))));

    if (paramInt("lfoSync") != 0) {
        bool playing = false;
        double ppq = 0.0, bpm = 120.0;
        if (auto* ph = getPlayHead()) {
            if (const auto pos = ph->getPosition()) {
                playing = pos->getIsPlaying();
                if (const auto q = pos->getPpqPosition()) ppq = *q;
                if (const auto b = pos->getBpm()) bpm = *b;
            }
        }
        const double beats = beatsForDivision(paramInt("lfoDivision"));
        if (playing) lfo.setPhase(ppq / beats);              // locked to the song
        else lfo.advance(dt, (bpm / 60.0) / beats);          // free-run at the synced rate when stopped
    } else {
        lfo.advance(dt, static_cast<double>(paramFloat("lfoRate")));
    }

    const int basePw = paramInt("pulseWidth");
    const int swing = static_cast<int>(lfo.value() * (paramInt("lfoDepth") / 100.0) * 2047.0);
    const int modPw = juce::jlimit(0, 4095, basePw + swing);
    if (lastModPw >= 0 && std::abs(modPw - lastModPw) < 8) return;  // skip inaudible steps
    lastModPw = modPw;

    // Stream the pulse-width register once. This is a continuous stream, so the
    // next update (or any note) flushes each step in, which halves the traffic
    // versus double-sending every frame.
    sendAsid(asidPlayer.setPulseWidth(voice, modPw));
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
    midiHub.sendMessage(juce::MidiMessage::createSysExMessage(
        asidMessage.data() + 1, static_cast<int>(asidMessage.size()) - 2));
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

    dbgPlaying.store(playing ? 1 : 0);
    dbgPlayheadSec.store(blockPlayheadMs / 1000.0);
    dbgAlignMs.store(playing ? juce::jmax(0.0, (blockPlayheadMs - nowMs) - refOffset) : 0.0);

    // Frames are stamped as millisecond offsets from nowMs (sampleRate 1000, so
    // one "sample" is one ms), then handed to the timed background sender.
    juce::MidiBuffer out;

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

        // Watch the real gate bit across the event to tell an attack (0->1) from
        // a legato pitch change (1->1) or a release (1->0).
        const bool gateBefore = (asidPlayer.state().control(voice) & sid::kGate) != 0;
        const auto frames = on ? asidPlayer.noteOn(ch, m.getNoteNumber(), m.getVelocity())
                               : asidPlayer.noteOff(ch, m.getNoteNumber());
        const bool gateAfter = (asidPlayer.state().control(voice) & sid::kGate) != 0;

        double target = juce::jmax(eventMs, voiceClockMs);  // never before the last frame
        if (gateAfter && !gateBefore)                       // attack: guarantee the gate-low window
            target = juce::jmax(target, gateLowMs + kMinGateLowMs);

        const int posMs = juce::jmax(0, juce::roundToInt(target - nowMs));
        for (const auto& f : frames) addFrame(out, f, posMs);

        voiceClockMs = target;
        if (!gateAfter && gateBefore) gateLowMs = target;   // release starts the window
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
    updateModulation(voice);

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
