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

class AsidShared {
public:
    struct Client {
        virtual ~Client() = default;
        // Read AsidShared::get() values and apply them to this instance.
        virtual void sharedUpdated() = 0;
    };

    static AsidShared& get() {
        static AsidShared instance;
        return instance;
    }

    static bool isShared(const juce::String& id) {
        return id == "cutoff" || id == "resonance" || id == "filterMode" || id == "volume"
            || id == "latency";
    }

    void addClient(Client* c) {
        const juce::ScopedLock sl(lock);
        clients.addIfNotAlreadyThere(c);
    }
    void removeClient(Client* c) {
        const juce::ScopedLock sl(lock);
        clients.removeFirstMatchingValue(c);
    }

    // Stores new shared values and tells every client except the source.
    void publish(int cutoff_, int resonance_, int mode_, int volume_, int latency_, Client* source) {
        cutoff.store(cutoff_);
        resonance.store(resonance_);
        mode.store(mode_);
        volume.store(volume_);
        latency.store(latency_);
        hasData.store(true);

        juce::Array<Client*> copy;
        { const juce::ScopedLock sl(lock); copy = clients; }
        for (auto* c : copy)
            if (c != source) c->sharedUpdated();
    }

    int valueFor(const juce::String& id) const {
        if (id == "cutoff") return cutoff.load();
        if (id == "resonance") return resonance.load();
        if (id == "filterMode") return mode.load();
        if (id == "volume") return volume.load();
        if (id == "latency") return latency.load();
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

    // The filter routing register holds one bit per voice, all in one shared
    // register. Each instance sets its own voice's bit here so any instance can
    // write the full byte without wiping the others.
    void setRoutingBit(int voice, bool on) {
        if (voice < 0 || voice > 2) return;
        const int bit = 1 << voice;  // voice 0/1/2 -> bit 1/2/4
        int r = routing.load();
        r = on ? (r | bit) : (r & ~bit);
        routing.store(r);
    }

    std::atomic<int> cutoff{2047}, resonance{0}, mode{0}, volume{15};
    std::atomic<int> routing{0};
    std::atomic<int> latency{0};  // ms added to each note's scheduled play time
    std::atomic<bool> hasData{false};

    std::atomic<double> refOffsetMs{1.0e18};  // running min of playheadMs - wallMs

private:
    juce::CriticalSection lock;
    juce::Array<Client*> clients;
};
