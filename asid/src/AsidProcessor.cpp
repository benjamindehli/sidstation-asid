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

    // Filter. Cutoff, resonance and mode are shared by all voices; routing is
    // per voice (whether this instance's voice goes through the filter).
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"filterRoute", 1}, "Route Through Filter", false));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("cutoff", "Cutoff", 0, 2047, 2047)));
    layout.add(std::unique_ptr<juce::AudioParameterInt>(intParam("resonance", "Resonance", 0, 15, 0)));
    layout.add(std::make_unique<Choice>(juce::ParameterID{"filterMode", 1}, "Filter Mode",
                                        juce::StringArray{"Low", "Band", "High"}, 0));
    return layout;
}

int AsidProcessor::paramInt(const char* id) const {
    if (auto* p = apvts.getRawParameterValue(id)) return static_cast<int>(std::lround(p->load()));
    return 0;
}

void AsidProcessor::applyControlChanges(int voice, bool forceAll) {
    if (voice != sent.voice) { forceAll = true; sent.voice = voice; }
    auto flush = [&](const Bytes& f) {
        if (!f.empty()) { queueAsid(f); queueAsid(f); }  // send twice to apply
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
    const int route = paramInt("filterRoute");
    if (forceAll || route != sent.route) {
        sent.route = route;
        flush(asidPlayer.setFilterRouting(voice, route != 0));
    }
    const int cutoff = paramInt("cutoff");
    if (forceAll || cutoff != sent.cutoff) { sent.cutoff = cutoff; flush(asidPlayer.setCutoff(cutoff)); }
    const int res = paramInt("resonance");
    if (forceAll || res != sent.resonance) { sent.resonance = res; flush(asidPlayer.setResonance(res)); }
    const int mode = paramInt("filterMode");
    if (forceAll || mode != sent.mode) {
        sent.mode = mode;
        flush(asidPlayer.setFilterMode(kMode[juce::jlimit(0, 2, mode)]));
    }
}

AsidProcessor::AsidProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ASID", makeLayout()) {
    startTimer(15);  // drain queued ASID frames to the device
}

AsidProcessor::~AsidProcessor() { stopTimer(); }

bool AsidProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void AsidProcessor::queueAsid(const Bytes& asidMessage) {
    if (asidMessage.size() < 2) return;
    auto msg = juce::MidiMessage::createSysExMessage(
        asidMessage.data() + 1, static_cast<int>(asidMessage.size()) - 2);
    const juce::SpinLock::ScopedLockType sl(pendingLock);
    pending.add(msg);
}

void AsidProcessor::timerCallback() {
    juce::Array<juce::MidiMessage> toSend;
    {
        const juce::SpinLock::ScopedLockType sl(pendingLock);
        toSend.swapWith(pending);
    }
    for (const auto& m : toSend)
        midiHub.sendMessage(m);
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
        for (const auto& msg : asidPlayer.start()) { queueAsid(msg); queueAsid(msg); }
        forceControls = true;  // push the current control values after the start state
    }

    applyControlChanges(voice, forceControls);

    for (const auto meta : midiMessages) {
        const auto m = meta.getMessage();
        const int ch = m.getChannel() - 1;
        if (m.isNoteOn())
            for (const auto& frame : asidPlayer.noteOn(ch, m.getNoteNumber(), m.getVelocity()))
                queueAsid(frame);
        else if (m.isNoteOff())
            for (const auto& frame : asidPlayer.noteOff(ch, m.getNoteNumber()))
                queueAsid(frame);
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
