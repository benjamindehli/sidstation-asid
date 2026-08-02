#include "MidiHub.h"

using namespace sidstation;

MidiHub::~MidiHub() {
    closeInput();
    closeOutput();
}

bool MidiHub::openOutputByIdentifier(const juce::String& identifier) {
    auto dev = juce::MidiOutput::openDevice(identifier);
    if (dev == nullptr) return false;
    const juce::ScopedLock sl(outputLock);
    output = std::move(dev);
    outputInfo = output->getDeviceInfo();
    return true;
}

bool MidiHub::openInputByIdentifier(const juce::String& identifier) {
    auto dev = juce::MidiInput::openDevice(identifier, this);
    if (dev == nullptr) return false;
    input = std::move(dev);
    inputInfo = input->getDeviceInfo();
    input->start();
    return true;
}

static juce::String findIdentifierByName(const juce::Array<juce::MidiDeviceInfo>& devices,
                                         const juce::String& nameSubstr) {
    for (const auto& d : devices)
        if (d.name.containsIgnoreCase(nameSubstr)) return d.identifier;
    return {};
}

bool MidiHub::openOutputMatching(const juce::String& nameSubstr) {
    auto id = findIdentifierByName(availableOutputs(), nameSubstr);
    return id.isNotEmpty() && openOutputByIdentifier(id);
}

bool MidiHub::openInputMatching(const juce::String& nameSubstr) {
    auto id = findIdentifierByName(availableInputs(), nameSubstr);
    return id.isNotEmpty() && openInputByIdentifier(id);
}

void MidiHub::closeOutput() {
    const juce::ScopedLock sl(outputLock);
    output.reset();
    outputInfo = {};
}

void MidiHub::closeInput() {
    if (input != nullptr) input->stop();
    input.reset();
    inputInfo = {};
}

void MidiHub::sendSysEx(const Bytes& fullMessage) {
    if (fullMessage.size() < 2) return;  // needs at least F0 F7
    // createSysExMessage takes the inner bytes and re-adds F0/F7.
    auto msg = juce::MidiMessage::createSysExMessage(
        fullMessage.data() + 1, static_cast<int>(fullMessage.size()) - 2);
    sendMessage(msg);
}

void MidiHub::sendMessage(const juce::MidiMessage& m) {
    const juce::ScopedLock sl(outputLock);
    if (output != nullptr) output->sendMessageNow(m);
}

void MidiHub::sendPaced(const std::vector<Bytes>& messages, int delayMs) {
    const juce::ScopedLock sl(outputLock);
    if (output == nullptr || messages.empty()) return;

    // Build a buffer whose event timestamps are milliseconds (we tell JUCE the
    // "sample rate" is 1000, so 1 sample == 1 ms). sendBlockOfMessages then
    // paces them out on its own background thread without blocking the UI.
    juce::MidiBuffer buffer;
    int timeMs = 0;
    for (const auto& m : messages) {
        if (m.size() < 2) continue;
        buffer.addEvent(juce::MidiMessage::createSysExMessage(
                            m.data() + 1, static_cast<int>(m.size()) - 2),
                        timeMs);
        timeMs += juce::jmax(1, delayMs);
    }
    output->sendBlockOfMessages(buffer,
                                juce::Time::getMillisecondCounter(),
                                1000.0 /* samples (ms) per second */);
}

void MidiHub::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) {
    if (!message.isSysEx() || listener == nullptr) return;
    // getRawData() includes the F0..F7 framing for a SysEx message.
    const auto* raw = message.getRawData();
    Bytes bytes(raw, raw + message.getRawDataSize());
    if (auto patch = decodePatchDump(bytes))
        listener->midiPatchReceived(*patch, bytes);
}
