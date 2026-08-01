#include "PluginProcessor.h"

#include <cmath>

#include "sidstation/DirectProgram.h"

using namespace sidstation;

namespace {
// A neutral default: 0 where it's in range, otherwise the nearest bound.
int defaultFor(const ParamInfo& p) {
    return juce::jlimit(p.minValue, p.maxValue, 0);
}
// std::string -> juce::String (ids/names are ASCII).
juce::String S(const std::string& s) { return juce::String(s.c_str()); }
}  // namespace

juce::AudioProcessorValueTreeState::ParameterLayout
SidStationAudioProcessor::makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& p : parameters()) {
        juce::ParameterID pid{S(p.id), 1};
        if (p.kind == ParamKind::Bool) {
            layout.add(std::make_unique<juce::AudioParameterBool>(pid, S(p.name), false));
        } else {
            layout.add(std::make_unique<juce::AudioParameterInt>(
                pid, S(p.name), p.minValue, p.maxValue, defaultFor(p)));
        }
    }
    return layout;
}

SidStationAudioProcessor::SidStationAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", makeLayout()) {
    for (const auto& p : parameters()) {
        idToInfo[p.id] = &p;
        apvts.addParameterListener(S(p.id), this);
    }
}

SidStationAudioProcessor::~SidStationAudioProcessor() {
    for (const auto& p : parameters())
        apvts.removeParameterListener(S(p.id), this);
}

void SidStationAudioProcessor::prepareToPlay(double, int) {}

bool SidStationAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SidStationAudioProcessor::queueDirectProgram(const ParamInfo& info, int value) {
    Bytes dp = directProgramFor(info, value);  // full F0..F7
    // JUCE's createSysExMessage takes the inner bytes and adds F0/F7 itself.
    auto msg = juce::MidiMessage::createSysExMessage(dp.data() + 1,
                                                     static_cast<int>(dp.size()) - 2);
    const juce::SpinLock::ScopedLockType sl(pendingLock);
    pending.add(msg);
}

void SidStationAudioProcessor::parameterChanged(const juce::String& parameterID,
                                                float newValue) {
    if (suppressSending.load()) return;
    auto it = idToInfo.find(parameterID.toStdString());
    if (it == idToInfo.end()) return;
    queueDirectProgram(*it->second, static_cast<int>(std::lround(newValue)));
}

void SidStationAudioProcessor::sendAllParameters() {
    for (const auto& p : parameters()) {
        if (auto* param = apvts.getRawParameterValue(S(p.id)))
            queueDirectProgram(p, static_cast<int>(std::lround(param->load())));
    }
}

void SidStationAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    // Instrument scaffold: no audio synthesis yet (the sound comes from the
    // hardware). Output silence; audio pass-through from the unit is a later
    // milestone.
    buffer.clear();

    // Incoming MIDI (notes, etc.) already sits in midiMessages and is passed
    // through to the hardware. Append any queued Direct-Program edits.
    const juce::SpinLock::ScopedTryLockType tl(pendingLock);
    if (tl.isLocked() && !pending.isEmpty()) {
        for (const auto& m : pending)
            midiMessages.addEvent(m, 0);
        pending.clearQuick();
    }
}

juce::AudioProcessorEditor* SidStationAudioProcessor::createEditor() {
    // Scaffold: an auto-generated editor exposing every parameter. Replaced by
    // a custom GUI in a later milestone.
    return new juce::GenericAudioProcessorEditor(*this);
}

void SidStationAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void SidStationAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    // Restore without blasting DP messages at the hardware.
    suppressSending.store(true);
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
    suppressSending.store(false);
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SidStationAudioProcessor();
}
