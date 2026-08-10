#include "MidiHub.h"

#include <cstring>
#include <queue>
#include <vector>

MidiHub::~MidiHub() {
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

// Producer side (audio thread): copy the frame into the ring, no allocation.
//
// pushLock guards the write side and the sequence counter, because
// juce::AbstractFifo is single-producer and this one hub takes pushes from
// every instance's audio thread plus the AsidShared watchdog thread. That means
// the audio thread does briefly block on a mutex, which is worth being honest
// about; it is bounded by one 64-byte memcpy with no allocation or syscall
// inside, and the watchdog releases and retakes it per frame rather than
// holding it across its burst. The single sender thread is the only reader, so
// it needs no lock of its own.
void MidiHub::pushFrame(const juce::uint8 *data, int len, double timeMs) {
  if (len <= 0 || len > static_cast<int>(sizeof(Frame::data)))
    return;
  // With no port open there is no consumer: the sender thread only runs while a
  // device is. Queueing anyway filled the ring and then dumped the whole
  // backlog at the unit the moment a device was picked, every frame already
  // past due.
  if (!hasOutput())
    return;
  const juce::ScopedLock sl(pushLock);
  int start1, size1, start2, size2;
  frameFifo.prepareToWrite(1, start1, size1, start2, size2);
  if (size1 > 0) {
    Frame &f = frameStore[static_cast<size_t>(start1)];
    f.timeMs = timeMs;
    f.seq = frameSeq++;
    f.len = len;
    std::memcpy(f.data, data, static_cast<size_t>(len));
    frameFifo.finishedWrite(1);
  }
  // FIFO full (should not happen at these rates): drop rather than block.
}

// Consumer side: drain the ring into a time-ordered heap and send each frame at
// its due time. Runs on its own thread, so CoreMIDI never touches the audio
// thread.
void MidiHub::Sender::run() {
  struct Pending {
    double timeMs;
    long long seq;
    juce::MidiMessage msg;
  };
  // Min-heap by send time, then by insertion order so same-time frames (a
  // note-on's sequence of writes) never get reordered.
  struct Later {
    bool operator()(const Pending &a, const Pending &b) const {
      // Ordered comparisons only, no float equality: same-time frames fall
      // through to the insertion counter, which is the tie-break that keeps a
      // note-on's writes in sequence.
      if (a.timeMs < b.timeMs)
        return false;
      if (b.timeMs < a.timeMs)
        return true;
      return a.seq > b.seq;
    }
  };
  std::priority_queue<Pending, std::vector<Pending>, Later> pending;

  while (!threadShouldExit()) {
    if (const int ready = hub.frameFifo.getNumReady(); ready > 0) {
      int s1, n1, s2, n2;
      hub.frameFifo.prepareToRead(ready, s1, n1, s2, n2);
      for (int i = 0; i < n1; ++i) {
        const Frame &f = hub.frameStore[static_cast<size_t>(s1 + i)];
        pending.push({f.timeMs, f.seq, juce::MidiMessage(f.data, f.len)});
      }
      for (int i = 0; i < n2; ++i) {
        const Frame &f = hub.frameStore[static_cast<size_t>(s2 + i)];
        pending.push({f.timeMs, f.seq, juce::MidiMessage(f.data, f.len)});
      }
      hub.frameFifo.finishedRead(n1 + n2);
    }

    const double now = juce::Time::getMillisecondCounterHiRes();
    while (!pending.empty() && pending.top().timeMs <= now) {
      // Drop a frame that is stale rather than late (see kMaxLateMs): replaying
      // a backlog of old gates and pitches at the unit is worse than silence.
      if (now - pending.top().timeMs <= kMaxLateMs) {
        const juce::ScopedLock sl(hub.outputLock);
        if (hub.output != nullptr)
          hub.output->sendMessageNow(pending.top().msg);
      }
      pending.pop();
    }

    // Sleep until the next frame is due (capped), woken early by a new push.
    double waitMs = 25.0;
    if (!pending.empty())
      waitMs = juce::jlimit(1.0, 25.0,
                            pending.top().timeMs -
                                juce::Time::getMillisecondCounterHiRes());
    wait(static_cast<int>(waitMs));
  }
}

bool MidiHub::openOutputByIdentifier(const juce::String &identifier) {
  auto dev = juce::MidiOutput::openDevice(identifier);
  if (dev == nullptr)
    return false;
  const juce::ScopedLock sl(outputLock);
  output = std::move(dev);
  // No startBackgroundThread(): that existed for sendBlockOfMessages, which
  // only the removed paced bulk-send path used. Our own sender thread calls
  // sendMessageNow.
  outputInfo = output->getDeviceInfo();
  startSender(); // real-time frames go through our own thread, not the audio
                 // thread
  outputOpen.store(true,
                   std::memory_order_release); // opens the gate in pushFrame
  return true;
}

static juce::String
findIdentifierByName(const juce::Array<juce::MidiDeviceInfo> &devices,
                     const juce::String &nameSubstr) {
  for (const auto &d : devices)
    if (d.name.containsIgnoreCase(nameSubstr))
      return d.identifier;
  return {};
}

bool MidiHub::openOutputMatching(const juce::String &nameSubstr) {
  auto id = findIdentifierByName(availableOutputs(), nameSubstr);
  return id.isNotEmpty() && openOutputByIdentifier(id);
}

void MidiHub::closeOutput() {
  const juce::ScopedLock sl(outputLock);
  outputOpen.store(false, std::memory_order_release); // stop producers first
  output.reset();
  outputInfo = {};
}

void MidiHub::sendScheduled(const juce::MidiBuffer &buffer, double startTimeMs,
                            double sampleRate) {
  if (buffer.isEmpty())
    return;
  // Push each event with its absolute send time; the sender thread delivers it.
  const double sr = juce::jmax(1.0, sampleRate);
  // Usable from the audio callback: no CoreMIDI call and no allocation. Note it
  // is not lock-free - pushFrame takes pushLock for a bounded memcpy (see
  // there).
  for (const auto meta : buffer) {
    const auto m = meta.getMessage();
    pushFrame(m.getRawData(), m.getRawDataSize(),
              startTimeMs + meta.samplePosition * 1000.0 / sr);
  }
  if (sender)
    sender->notify();
}
