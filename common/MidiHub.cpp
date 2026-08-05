#include "MidiHub.h"

#include <cstring>
#include <queue>
#include <vector>

using namespace sidstation;

MidiHub::~MidiHub() {
    closeInput();
    closeOutput();
    stopSender();
}

void MidiHub::startSender() {
    if (!sender) {
        sender = std::make_unique<Sender>(*this);
        sender->startThread();
    }
}

void MidiHub::stopSender() {
    if (sender) {
        sender->signalThreadShouldExit();
        sender->notify();
        sender->stopThread(1000);
        sender.reset();
    }
}

// Producer side (audio/mod thread): copy the frame into the ring, no allocation.
// One shared output takes pushes from every instance, so guard the write side and
// the sequence counter; the single sender thread stays the only reader.
void MidiHub::pushFrame(const juce::uint8* data, int len, double timeMs) {
    if (len <= 0 || len > static_cast<int>(sizeof(Frame::data))) return;
    const juce::ScopedLock sl(pushLock);
    int start1, size1, start2, size2;
    frameFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 > 0) {
        Frame& f = frameStore[start1];
        f.timeMs = timeMs;
        f.seq = frameSeq++;
        f.len = len;
        std::memcpy(f.data, data, static_cast<size_t>(len));
        frameFifo.finishedWrite(1);
    }
    // FIFO full (should not happen at these rates): drop rather than block.
}

// Consumer side: drain the ring into a time-ordered heap and send each frame at
// its due time. Runs on its own thread, so CoreMIDI never touches the audio thread.
void MidiHub::Sender::run() {
    struct Pending { double timeMs; long long seq; juce::MidiMessage msg; };
    // Min-heap by send time, then by insertion order so same-time frames (a
    // note-on's sequence of writes) never get reordered.
    struct Later {
        bool operator()(const Pending& a, const Pending& b) const {
            return a.timeMs != b.timeMs ? a.timeMs > b.timeMs : a.seq > b.seq;
        }
    };
    std::priority_queue<Pending, std::vector<Pending>, Later> pending;

    while (!threadShouldExit()) {
        if (const int ready = hub.frameFifo.getNumReady(); ready > 0) {
            int s1, n1, s2, n2;
            hub.frameFifo.prepareToRead(ready, s1, n1, s2, n2);
            for (int i = 0; i < n1; ++i) {
                const Frame& f = hub.frameStore[s1 + i];
                pending.push({f.timeMs, f.seq, juce::MidiMessage(f.data, f.len)});
            }
            for (int i = 0; i < n2; ++i) {
                const Frame& f = hub.frameStore[s2 + i];
                pending.push({f.timeMs, f.seq, juce::MidiMessage(f.data, f.len)});
            }
            hub.frameFifo.finishedRead(n1 + n2);
        }

        const double now = juce::Time::getMillisecondCounterHiRes();
        while (!pending.empty() && pending.top().timeMs <= now) {
            {
                const juce::ScopedLock sl(hub.outputLock);
                if (hub.output != nullptr) hub.output->sendMessageNow(pending.top().msg);
            }
            pending.pop();
        }

        // Sleep until the next frame is due (capped), woken early by a new push.
        double waitMs = 25.0;
        if (!pending.empty())
            waitMs = juce::jlimit(1.0, 25.0, pending.top().timeMs - juce::Time::getMillisecondCounterHiRes());
        wait(static_cast<int>(waitMs));
    }
}

bool MidiHub::openOutputByIdentifier(const juce::String& identifier) {
    auto dev = juce::MidiOutput::openDevice(identifier);
    if (dev == nullptr) return false;
    const juce::ScopedLock sl(outputLock);
    output = std::move(dev);
    // Required for sendBlockOfMessages (the paced patch dumps) to deliver.
    output->startBackgroundThread();
    outputInfo = output->getDeviceInfo();
    startSender();  // real-time frames go through our own thread, not the audio thread
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
    if (output != nullptr) output->stopBackgroundThread();
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

void MidiHub::sendPacedMessages(const std::vector<juce::MidiMessage>& messages, int delayMs) {
    const juce::ScopedLock sl(outputLock);
    if (output == nullptr || messages.empty()) return;
    juce::MidiBuffer buffer;
    int timeMs = 0;
    for (const auto& m : messages) {
        buffer.addEvent(m, timeMs);
        timeMs += juce::jmax(1, delayMs);
    }
    output->sendBlockOfMessages(buffer, juce::Time::getMillisecondCounter(), 1000.0);
}

void MidiHub::sendScheduled(const juce::MidiBuffer& buffer, double startTimeMs, double sampleRate) {
    if (buffer.isEmpty()) return;
    // Push each event with its absolute send time; the sender thread delivers it.
    // No CoreMIDI call and no lock here, so this is safe from the audio callback.
    const double sr = juce::jmax(1.0, sampleRate);
    for (const auto meta : buffer) {
        const auto m = meta.getMessage();
        pushFrame(m.getRawData(), m.getRawDataSize(), startTimeMs + meta.samplePosition * 1000.0 / sr);
    }
    if (sender) sender->notify();
}

void MidiHub::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) {
    if (!message.isSysEx() || listener == nullptr) return;
    // getRawData() includes the F0..F7 framing for a SysEx message.
    const auto* raw = message.getRawData();
    Bytes bytes(raw, raw + message.getRawDataSize());
    if (auto patch = decodePatchDump(bytes))
        listener->midiPatchReceived(*patch, bytes);
}
