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

static const char* kSharedIds[] = {"cutoff", "resonance", "filterMode", "volume"};

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
               paramInt("volume"), this);
}

void AsidProcessor::sharedUpdated() {
    auto& sh = AsidShared::get();
    setParamValue("cutoff", sh.cutoff.load());
    setParamValue("resonance", sh.resonance.load());
    setParamValue("filterMode", sh.mode.load());
    setParamValue("volume", sh.volume.load());
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
    // Sent straight from the audio callback so all instances go out together,
    // which keeps the voices tight. MidiHub locks its own output briefly.
    midiHub.sendMessage(juce::MidiMessage::createSysExMessage(
        asidMessage.data() + 1, static_cast<int>(asidMessage.size()) - 2));
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

    applyControlChanges(voice, forceControls);

    for (const auto meta : midiMessages) {
        const auto m = meta.getMessage();
        const int ch = m.getChannel() - 1;
        if (m.isNoteOn())
            for (const auto& frame : asidPlayer.noteOn(ch, m.getNoteNumber(), m.getVelocity()))
                sendAsid(frame);
        else if (m.isNoteOff())
            for (const auto& frame : asidPlayer.noteOff(ch, m.getNoteNumber()))
                sendAsid(frame);
    }

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
