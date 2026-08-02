#include "PluginProcessor.h"

#include <cmath>

#include "PluginEditor.h"
#include "sidstation/ControllerMap.h"
#include "sidstation/DirectProgram.h"
#include "sidstation/SyxFile.h"

using namespace sidstation;

namespace {
// The SidStation's MIDI base channel (1..16). Its default is 1.
constexpr int kBaseChannel = 1;
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
    midiHub.setListener(this);
    startTimer(15);  // drain queued edits to the device
}

SidStationAudioProcessor::~SidStationAudioProcessor() {
    stopTimer();
    for (const auto& p : parameters())
        apvts.removeParameterListener(S(p.id), this);
}

void SidStationAudioProcessor::prepareToPlay(double, int) {}

bool SidStationAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

juce::MidiMessage SidStationAudioProcessor::messageForParam(const ParamInfo& p, int value) {
    if (p.cc >= 0)
        return juce::MidiMessage::controllerEvent(kBaseChannel, p.cc, ccValue(p, value));
    // No CC: fall back to Direct Program (does nothing on OS 1.11, harmless).
    Bytes dp = directProgramFor(p, value);
    return juce::MidiMessage::createSysExMessage(dp.data() + 1,
                                                 static_cast<int>(dp.size()) - 2);
}

void SidStationAudioProcessor::queueParamChange(const ParamInfo& info, int value) {
    auto msg = messageForParam(info, value);
    const juce::SpinLock::ScopedLockType sl(pendingLock);
    pending.add(msg);
}

void SidStationAudioProcessor::parameterChanged(const juce::String& parameterID,
                                                float newValue) {
    if (suppressSending.load()) return;
    auto it = idToInfo.find(parameterID.toStdString());
    if (it == idToInfo.end()) return;
    queueParamChange(*it->second, static_cast<int>(std::lround(newValue)));
}

void SidStationAudioProcessor::sendAllParameters() {
    // A full parameter push is a bulk transfer, pace it so the unit keeps up.
    std::vector<juce::MidiMessage> msgs;
    for (const auto& p : parameters())
        if (auto* param = apvts.getRawParameterValue(S(p.id)))
            msgs.push_back(messageForParam(p, static_cast<int>(std::lround(param->load()))));
    midiHub.sendPacedMessages(msgs, 15);
}

void SidStationAudioProcessor::sendSyxToUnit(const Bytes& data) {
    // Bulk .syx (single patch, or a whole bank of ~190 messages) must be paced.
    midiHub.sendPaced(splitSysExMessages(data), 25);
}

void SidStationAudioProcessor::timerCallback() {
    // Move queued edits out under the lock, then send without holding it.
    juce::Array<juce::MidiMessage> toSend;
    {
        const juce::SpinLock::ScopedLockType sl(pendingLock);
        toSend.swapWith(pending);
    }
    for (const auto& m : toSend)
        midiHub.sendMessage(m);
}

void SidStationAudioProcessor::midiPatchReceived(const Patch& patch, const Bytes& raw) {
    const juce::ScopedLock sl(recvLock);
    received = ReceivedPatch{patch, raw};
}

std::optional<SidStationAudioProcessor::ReceivedPatch>
SidStationAudioProcessor::takeReceivedPatch() {
    const juce::ScopedLock sl(recvLock);
    auto out = received;
    received.reset();
    return out;
}

void SidStationAudioProcessor::queueAsid(const Bytes& asidMessage) {
    if (asidMessage.size() < 2) return;
    auto msg = juce::MidiMessage::createSysExMessage(
        asidMessage.data() + 1, static_cast<int>(asidMessage.size()) - 2);
    const juce::SpinLock::ScopedLockType sl(pendingLock);
    pending.add(msg);
}

void SidStationAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    // Apply a pending ASID mode start/stop request (audio thread owns the player).
    if (const int req = asidRequest.exchange(0)) {
        if (req == 1) {
            asidPlayer.reset();
            asidMode.store(true);
            for (const auto& msg : asidPlayer.start()) queueAsid(msg);
        } else {
            asidMode.store(false);
            for (const auto& msg : asidPlayer.stop()) queueAsid(msg);
        }
    }

    // ASID play: notes on channels 1/2/3 drive SID voices 1/2/3 directly.
    if (asidMode.load()) {
        for (const auto meta : midiMessages) {
            const auto m = meta.getMessage();
            const int ch = m.getChannel() - 1;  // JUCE channels are 1..16
            if (m.isNoteOn())
                queueAsid(asidPlayer.noteOn(ch, m.getNoteNumber(), m.getVelocity()));
            else if (m.isNoteOff())
                queueAsid(asidPlayer.noteOff(ch, m.getNoteNumber()));
        }
    }

    // No audio synthesis: sound comes from the hardware. MIDI to the SidStation
    // goes out via the directly-opened device (MidiHub), not this buffer.
    buffer.clear();
}

juce::AudioProcessorEditor* SidStationAudioProcessor::createEditor() {
    return new SidStationEditor(*this);
}

void SidStationAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void SidStationAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    suppressSending.store(true);
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
    suppressSending.store(false);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SidStationAudioProcessor();
}
