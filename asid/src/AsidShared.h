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
        return id == "cutoff" || id == "resonance" || id == "filterMode" || id == "volume";
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
    void publish(int cutoff_, int resonance_, int mode_, int volume_, Client* source) {
        cutoff.store(cutoff_);
        resonance.store(resonance_);
        mode.store(mode_);
        volume.store(volume_);
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
        return -1;
    }

    std::atomic<int> cutoff{2047}, resonance{0}, mode{0}, volume{15};
    std::atomic<bool> hasData{false};

private:
    juce::CriticalSection lock;
    juce::Array<Client*> clients;
};
