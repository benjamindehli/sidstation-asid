#include "AsidProcessor.h"

#include <cmath>

#include "AsidEditor.h"

using namespace sidstation;

namespace {
juce::AudioParameterInt* intParam(const char* id, const char* name, int lo, int hi, int def) {
    return new juce::AudioParameterInt(juce::ParameterID{id, 1}, name, lo, hi, def);
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
    return layout;
}

int AsidProcessor::paramInt(const char* id) const {
    if (auto* p = apvts.getRawParameterValue(id)) return static_cast<int>(std::lround(p->load()));
    return 0;
}

void AsidProcessor::applyControlChanges(int voice, bool forceAll) {
    if (voice != sent.voice) { forceAll = true; sent.voice = voice; }
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
    if (forceAll || pw != sent.pw) { sent.pw = pw; flush(asidPlayer.setPulseWidth(voice, pw)); }
    const int sync = paramInt("sync");
    if (forceAll || sync != sent.sync) { sent.sync = sync; flush(asidPlayer.setSync(voice, sync != 0)); }
    const int ring = paramInt("ring");
    if (forceAll || ring != sent.ring) { sent.ring = ring; flush(asidPlayer.setRing(voice, ring != 0)); }
    // Filter routing is chosen per voice but lives in a register shared by all
    // voices. Set our voice's bit in the shared routing, then write the whole
    // resonance+routing register so we never wipe the other voices' bits.
    const int route = paramInt("filterRoute");
    const int res = paramInt("resonance");
    bool reg17dirty = forceAll;
    if (forceAll || route != sent.route) {
        sent.route = route;
        AsidShared::get().setRoutingBit(voice, route != 0);
        reg17dirty = true;
    }
    if (forceAll || res != sent.resonance) { sent.resonance = res; reg17dirty = true; }
    if (reg17dirty)
        flush(asidPlayer.setResonanceRouting(res, AsidShared::get().routing.load()));

    // Shared controls.
    const int cutoff = paramInt("cutoff");
    if (forceAll || cutoff != sent.cutoff) { sent.cutoff = cutoff; flush(asidPlayer.setCutoff(cutoff)); }
    const int mode = paramInt("filterMode");
    if (forceAll || mode != sent.mode) {
        sent.mode = mode;
        flush(asidPlayer.setFilterMode(kMode[juce::jlimit(0, 2, mode)]));
    }
    const int vol = paramInt("volume");
    if (forceAll || vol != sent.volume) { sent.volume = vol; flush(asidPlayer.setVolume(vol)); }
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
    setParamValue("cutoff", sh.cutoff.load());
    setParamValue("resonance", sh.resonance.load());
    setParamValue("filterMode", sh.mode.load());
    setParamValue("volume", sh.volume.load());
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
    if (playing) AsidShared::get().reportPlayOffset(blockPlayheadMs - nowMs, nowMs);
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
        // Playing: place the note at its song position mapped through the shared
        // reference. Stopped or live: just now. The latency trim applies to both.
        const double eventMs = playing
                                   ? (blockPlayheadMs + sampleOffsetMs) - refOffset + latencyMs
                                   : nowMs + sampleOffsetMs + latencyMs;

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
