// MidiHub - direct MIDI device access, shared by both plugins.
//
// Rather than relying on the DAW to route MIDI, a plugin opens the USB-MIDI
// interface itself. This gives identical behaviour in any DAW and in Standalone,
// which is what a hardware editor and the ASID player both need.
//
// Immediate sends may come from the audio thread; timed sends (sendScheduled,
// sendPaced) hand off to JUCE's MIDI background thread. Incoming SysEx is decoded
// on JUCE's MIDI thread and handed to the listener, which must be thread-aware.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>
#include <vector>

#include "sidstation/Patch.h"
#include "sidstation/SysEx.h"

class MidiHub : private juce::MidiInputCallback {
public:
    struct Listener {
        virtual ~Listener() = default;
        // Called on the MIDI thread when a complete patch dump arrives. `raw` is
        // the exact SysEx bytes (F0..F7), suitable for writing straight to .syx.
        virtual void midiPatchReceived(const sidstation::Patch& patch,
                                       const sidstation::Bytes& raw) = 0;
    };

    MidiHub() = default;
    ~MidiHub() override;

    void setListener(Listener* l) { listener = l; }

    static juce::Array<juce::MidiDeviceInfo> availableOutputs() {
        return juce::MidiOutput::getAvailableDevices();
    }
    static juce::Array<juce::MidiDeviceInfo> availableInputs() {
        return juce::MidiInput::getAvailableDevices();
    }

    bool openOutputByIdentifier(const juce::String& identifier);
    bool openInputByIdentifier(const juce::String& identifier);
    // Open the first device whose name contains `nameSubstr` (case-insensitive).
    bool openOutputMatching(const juce::String& nameSubstr);
    bool openInputMatching(const juce::String& nameSubstr);
    void closeOutput();
    void closeInput();

    juce::String outputName() const { return outputInfo.name; }
    juce::String inputName() const { return inputInfo.name; }
    juce::String outputIdentifier() const { return outputInfo.identifier; }
    juce::String inputIdentifier() const { return inputInfo.identifier; }
    bool hasOutput() const { return output != nullptr; }

    // Sends one complete SysEx message (bytes must be F0..F7).
    void sendSysEx(const sidstation::Bytes& fullMessage);
    // Sends an arbitrary MIDI message (used to drain queued edits).
    void sendMessage(const juce::MidiMessage& m);
    // Sends many complete SysEx messages with `delayMs` between each, timed on a
    // background thread. The SidStation cannot receive bulk dumps at full MIDI
    // speed (per Elektron's C6 tool, 5-50 ms between packets), so patch/bank
    // transfers must be paced or they silently fail.
    void sendPaced(const std::vector<sidstation::Bytes>& messages, int delayMs);
    // Same idea for a list of arbitrary MIDI messages (used to pace a full CC
    // parameter push).
    void sendPacedMessages(const std::vector<juce::MidiMessage>& messages, int delayMs);
    // Delivers a block of MIDI on the background thread, timed to an absolute
    // wall-clock start (in the Time::getMillisecondCounter() base). Event sample
    // positions are offsets from that start at `sampleRate`. Used to align notes
    // to the host's play time so hardware and DAW agree.
    void sendScheduled(const juce::MidiBuffer& buffer, double startTimeMs, double sampleRate);

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;

    // Off-audio-thread MIDI delivery. Producers (the audio callback and the mod
    // streams) push frames with an absolute send time into a lock-free FIFO; a
    // dedicated sender thread sends each at its time via sendMessageNow. This
    // keeps CoreMIDI I/O off the audio callback, which Logic's audio/MIDI sync on
    // built-in audio is sensitive to.
    // seq is an insertion counter: it breaks ties so frames with the same send
    // time keep their order (a note-on's frames must stay in sequence).
    struct Frame { double timeMs = 0.0; long long seq = 0; int len = 0; juce::uint8 data[64] = {}; };
    static constexpr int kFifoCapacity = 4096;
    std::vector<Frame> frameStore{kFifoCapacity};
    juce::AbstractFifo frameFifo{kFifoCapacity};
    long long frameSeq = 0;                 // insertion counter (guarded by pushLock)
    juce::CriticalSection pushLock;         // several instances push into one shared out
    void pushFrame(const juce::uint8* data, int len, double timeMs);

    struct Sender : juce::Thread {
        MidiHub& hub;
        explicit Sender(MidiHub& h) : juce::Thread("SidStation MIDI sender"), hub(h) {}
        void run() override;  // drains the FIFO and sends frames at their times
    };
    std::unique_ptr<Sender> sender;
    void startSender();
    void stopSender();

    std::unique_ptr<juce::MidiOutput> output;
    std::unique_ptr<juce::MidiInput> input;
    juce::MidiDeviceInfo outputInfo, inputInfo;
    Listener* listener = nullptr;
    juce::CriticalSection outputLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiHub)
};
