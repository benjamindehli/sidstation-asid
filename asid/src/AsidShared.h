// Process-wide shared state for the ASID plugin.
//
// The SidStation has one physical filter and one master volume, shared by all
// three voices. Each plugin instance drives one voice, but the filter and
// volume are common. This little singleton lets every instance in the host
// process stay in sync: when one changes a shared control, the others follow.
//
// Note: this works when the instances share a process, which is the normal
// case for VST3 and in-process AU. A host that sandboxes each plugin instance
// into its own process would not share this.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

#include "MidiHub.h"

class AsidShared {
public:
    struct Client {
        virtual ~Client() = default;
        // Read AsidShared::get() values and apply them to this instance.
        virtual void sharedUpdated() = 0;
    };

    static AsidShared& get() {
        // Heap-allocated and intentionally never destroyed. The shared MidiHub's
        // MidiOutput must not be torn down at process exit: JUCE's CoreMIDI cleanup
        // then messages already-finalized Objective-C state and crashes the host on
        // quit. A static-lifetime instance would run that teardown via __cxa_finalize;
        // leaking it lets the OS reclaim everything at exit with no teardown.
        static AsidShared* instance = new AsidShared();
        return *instance;
    }

    static bool isShared(const juce::String& id) {
        return id == "cutoff" || id == "resonance" || id == "volume" || id == "latency"
            || id == "filt1" || id == "filt2" || id == "filt3" || id == "filtExt"
            || id == "modeLP" || id == "modeBP" || id == "modeHP" || id == "voice3off"
            || id == "modRate";
    }

    void addClient(Client* c) {
        const juce::ScopedLock sl(lock);
        clients.addIfNotAlreadyThere(c);
    }
    void removeClient(Client* c) {
        const juce::ScopedLock sl(lock);
        clients.removeFirstMatchingValue(c);
    }

    // Stores new shared values and tells every client except the source. routing
    // and mode are 3-bit masks (voice 1/2/3, and LP/BP/HP which combine).
    void publish(int cutoff_, int resonance_, int mode_, int routing_, int volume_, int latency_,
                 int modRate_, Client* source) {
        cutoff.store(cutoff_);
        resonance.store(resonance_);
        mode.store(mode_);
        routing.store(routing_);
        volume.store(volume_);
        latency.store(latency_);
        modRate.store(modRate_);
        hasData.store(true);

        juce::Array<Client*> copy;
        { const juce::ScopedLock sl(lock); copy = clients; }
        for (auto* c : copy)
            if (c != source) c->sharedUpdated();
    }

    int valueFor(const juce::String& id) const {
        if (id == "cutoff") return cutoff.load();
        if (id == "resonance") return resonance.load();
        if (id == "volume") return volume.load();
        if (id == "latency") return latency.load();
        if (id == "modRate") return modRate.load();
        if (id == "filt1") return (routing.load() >> 0) & 1;
        if (id == "filt2") return (routing.load() >> 1) & 1;
        if (id == "filt3") return (routing.load() >> 2) & 1;
        if (id == "filtExt") return (routing.load() >> 3) & 1;
        if (id == "modeLP") return (mode.load() >> 0) & 1;
        if (id == "modeBP") return (mode.load() >> 1) & 1;
        if (id == "modeHP") return (mode.load() >> 2) & 1;
        if (id == "voice3off") return (mode.load() >> 3) & 1;
        return -1;
    }

    // Playhead-to-wall alignment. Each instance reports its block's offset
    // (playheadMs - wallMs) while playing. The running minimum since the last
    // reset is the true song-to-wall mapping: it is captured at playback start,
    // before the host ramps its render lookahead, so it holds even when no track
    // is live to anchor it. Reset on transport start or a jump.
    void resetPlayReference() { refOffsetMs.store(1.0e18); }
    void reportPlayOffset(double offsetMs) {
        double cur = refOffsetMs.load();
        while (offsetMs < cur && !refOffsetMs.compare_exchange_weak(cur, offsetMs)) {}
    }
    double playOffset() const { return refOffsetMs.load(); }

    // Cutoff is one shared filter, so only one instance may modulate it at a
    // time. An instance claims it while its LFO targets cutoff; others targeting
    // cutoff stay idle until it is free.
    bool claimCutoffMod(Client* c) {
        Client* expected = nullptr;
        return cutoffModOwner.load() == c || cutoffModOwner.compare_exchange_strong(expected, c);
    }
    void releaseCutoffMod(Client* c) {
        Client* self = c;
        cutoffModOwner.compare_exchange_strong(self, nullptr);
    }
    bool isCutoffModOwner(Client* c) const { return cutoffModOwner.load() == c; }
    bool cutoffModActive() const { return cutoffModOwner.load() != nullptr; }

    // The filter routing (one bit per voice) and mode (LP/BP/HP, combinable) both
    // live in registers shared by all voices, and any instance can edit any bit,
    // so they are published as whole masks like cutoff and resonance.
    std::atomic<int> cutoff{2047}, resonance{0}, mode{1}, volume{15};  // mode bit0 = LP
    std::atomic<int> routing{0};
    std::atomic<int> modRate{1};  // shared modulation clock (0 Eco .. 3 Smooth), default PAL
    std::atomic<int> latency{0};  // ms added to each note's scheduled play time
    std::atomic<bool> hasData{false};

    std::atomic<double> refOffsetMs{1.0e18};  // running min of playheadMs - wallMs
    std::atomic<Client*> cutoffModOwner{nullptr};  // sole instance modulating the shared cutoff

    // One MIDI output for every instance, so all voices' frames leave as a single
    // time-ordered stream. Two independent senders to the SidStation interleave
    // and confuse its one-message-late handling, which mangled notes when two
    // voices played at once.
    MidiHub out;
    // Bumped when any instance (re)opens the shared device, so every instance
    // re-pushes its voice setup (voices that initialised before it was open would
    // otherwise stay silent).
    std::atomic<int> outGeneration{0};

    // Total bytes ever sent to the device across all instances. Monotonic, so an
    // editor derives the current rate from the delta over its own poll interval.
    // The SidStation's MIDI is 31250 baud, ~3125 bytes/sec, shared by every voice.
    static constexpr double kMidiBytesPerSec = 3125.0;
    std::atomic<long long> bytesSent{0};
    void addBytes(int n) { bytesSent.fetch_add(n > 0 ? n : 0, std::memory_order_relaxed); }

private:
    juce::CriticalSection lock;
    juce::Array<Client*> clients;
};
