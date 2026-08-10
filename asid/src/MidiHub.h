// MidiHub - direct MIDI device access.
//
// Rather than relying on the DAW to route MIDI, the plugin opens the USB-MIDI
// interface itself. This gives identical behaviour in any DAW and in
// Standalone, which is what the ASID player needs.
//
// Threading. sendScheduled is the real-time path: it copies each frame into a
// ring buffer and returns, and a MidiHub-owned sender thread does the CoreMIDI
// I/O at each frame's due time. It is NOT lock-free - pushFrame takes a short
// mutex, because one hub takes pushes from every plugin instance while
// juce::AbstractFifo only supports a single producer - but the guarded section
// is a bounded memcpy with no allocation and no syscall, which is what makes it
// usable from the audio callback.
//
// Output only, and deliberately: this plugin streams ASID one way. The MIDI
// input, patch-dump listener and paced bulk-send paths were for the patch
// editor this project began as, and nothing called them any more, so they are
// gone. The protocol side of that work still lives in core/ and is exercised by
// probe/ and the core tests.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <memory>
#include <vector>

class MidiHub {
public:
  MidiHub() = default;
  ~MidiHub();

  static juce::Array<juce::MidiDeviceInfo> availableOutputs() {
    return juce::MidiOutput::getAvailableDevices();
  }

  bool openOutputByIdentifier(const juce::String &identifier);
  // Open the first device whose name contains `nameSubstr` (case-insensitive).
  bool openOutputMatching(const juce::String &nameSubstr);
  void closeOutput();

  juce::String outputName() const { return outputInfo.name; }
  juce::String outputIdentifier() const { return outputInfo.identifier; }
  // Lock-free, so the audio thread can check it before building a frame. Kept
  // in step with `output` under outputLock.
  bool hasOutput() const { return outputOpen.load(std::memory_order_acquire); }

  // Delivers a block of MIDI on the background thread, timed to an absolute
  // wall-clock start (in the Time::getMillisecondCounter() base). Event sample
  // positions are offsets from that start at `sampleRate`. Used to align notes
  // to the host's play time so hardware and DAW agree.
  void sendScheduled(const juce::MidiBuffer &buffer, double startTimeMs,
                     double sampleRate);

private:
  // Off-audio-thread MIDI delivery. Producers (the audio callback and the mod
  // streams) push frames with an absolute send time into a lock-free FIFO; a
  // dedicated sender thread sends each at its time via sendMessageNow. This
  // keeps CoreMIDI I/O off the audio callback, which Logic's audio/MIDI sync on
  // built-in audio is sensitive to.
  // seq is an insertion counter: it breaks ties so frames with the same send
  // time keep their order (a note-on's frames must stay in sequence).
  struct Frame {
    double timeMs = 0.0;
    long long seq = 0;
    int len = 0;
    juce::uint8 data[64] = {};
  };
  static constexpr int kFifoCapacity = 4096;
  // A frame this far past its send time is stale, not merely late: the sender
  // delivers within ~25 ms when it is running, and nothing is ever scheduled
  // more than ~600 ms ahead. Anything older belongs to a backlog that built up
  // while the port was shut, and sending it would replay old gates at the unit.
  static constexpr double kMaxLateMs = 1000.0;
  std::vector<Frame> frameStore{kFifoCapacity};
  juce::AbstractFifo frameFifo{kFifoCapacity};
  long long frameSeq = 0;         // insertion counter (guarded by pushLock)
  juce::CriticalSection pushLock; // several instances push into one shared out
  void pushFrame(const juce::uint8 *data, int len, double timeMs);

  struct Sender : juce::Thread {
    MidiHub &hub;
    explicit Sender(MidiHub &h)
        : juce::Thread("SidStation MIDI sender"), hub(h) {}
    void run() override; // drains the FIFO and sends frames at their times
  };
  std::unique_ptr<Sender> sender;
  void startSender();
  void stopSender();

  std::unique_ptr<juce::MidiOutput> output;
  // Mirrors `output != nullptr` for lock-free reads (see hasOutput).
  std::atomic<bool> outputOpen{false};
  juce::MidiDeviceInfo outputInfo;
  juce::CriticalSection outputLock;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiHub)
};
