#include "AsidProcessor.h"

#include "AsidEditor.h"

using namespace sidstation;

juce::AudioProcessorValueTreeState::ParameterLayout AsidProcessor::makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // Which SID voice this instance drives. Saved per instance, so each track
    // can control a different voice.
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"asidVoice", 1}, "SID Voice",
        juce::StringArray{"Voice 1", "Voice 2", "Voice 3"}, 0));
    return layout;
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

    // This instance drives one SID voice. Set it before start/note handling.
    if (auto* vp = apvts.getRawParameterValue("asidVoice"))
        asidPlayer.setTargetVoice(static_cast<int>(vp->load()));

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

    if (asidMode.load()) {
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
