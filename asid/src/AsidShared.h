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
#include <map>

#include "MidiHub.h"
#include "sidstation/Asid.h"

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

    AsidShared() {
        for (int v = 0; v < 3; ++v) { voiceNote[v] = -1; voiceSeenMs[v] = 0.0; releaseGenV[v] = 0; }
        watchdog.startThread();  // never stopped (this singleton is intentionally leaked)
    }

    // Per-voice stuck-note watchdog. Each instance reports its voice's held note (or
    // -1) every processBlock; the watchdog thread - which stays alive even when a DAW
    // suspends an unselected track's processBlock and its editor timer - releases a
    // voice that stops being reported while a note is held. releaseGen(voice) bumps
    // when it does, so the instance clears its stale note on resume (no re-gate).
    void reportVoiceNote(int voice, int note, double nowMs) {
        if (voice < 0 || voice > 2) return;
        voiceSeenMs[voice].store(nowMs, std::memory_order_relaxed);
        voiceNote[voice].store(note, std::memory_order_relaxed);
    }
    int releaseGen(int voice) const {
        return (voice >= 0 && voice < 3) ? releaseGenV[voice].load(std::memory_order_relaxed) : 0;
    }
    // Release every voice right now (manual Panic button).
    void panicAllVoices() {
        const double now = juce::Time::getMillisecondCounterHiRes();
        lastPanicMs.store(now, std::memory_order_relaxed);
        for (int v = 0; v < 3; ++v) releaseStuckVoice(v, now);
    }
    // Same, for transport stop: every instance hits this on the same block, and a
    // burst of overlapping panics overruns the unit (a single one commits fine), so
    // the first instance wins the race and the rest skip.
    void panicOnStop() {
        const double now = juce::Time::getMillisecondCounterHiRes();
        double last = lastPanicMs.load(std::memory_order_relaxed);
        if (now - last < 150.0) return;
        if (!lastPanicMs.compare_exchange_strong(last, now)) return;
        for (int v = 0; v < 3; ++v) releaseStuckVoice(v, now);
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
        clientVoice.erase(c);
    }

    // ---- Voice ownership (which instance drives which SID voice 0..2) ----
    // Each instance registers the voice it drives, so the editor can mark and block
    // voices already taken by another instance (one instance per voice).
    void setClientVoice(Client* c, int voice) {
        const juce::ScopedLock sl(lock);
        clientVoice[c] = voice;
    }
    // How many instances drive a voice, optionally excluding one (to answer "is any
    // OTHER instance already on this voice?").
    int usersOnVoice(int voice, const Client* except = nullptr) const {
        const juce::ScopedLock sl(lock);
        int n = 0;
        for (const auto& kv : clientVoice)
            if (kv.second == voice && kv.first != except) ++n;
        return n;
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
    // Send a repeated, spaced hard gate-off (control register = 0: gate low, no
    // waveform) so it commits on the SidStation's one-update-per-frame handling.
    void releaseStuckVoice(int voice, double nowMs) {
        voiceNote[voice].store(-1, std::memory_order_relaxed);
        releaseGenV[voice].fetch_add(1, std::memory_order_relaxed);
        const int base = sidstation::SidState::voiceBase(voice);
        const auto frame = sidstation::encodeAsidUpdate(
            {{static_cast<sidstation::Byte>(base + 4), static_cast<sidstation::Byte>(0)}});
        // Repeat over a window longer than the note stream's max schedule-ahead (a
        // playing track aligns frames up to ~500 ms into the future for the DAW's
        // render-ahead). The first frame releases at once; later ones land after any
        // gate-on frame still queued from before the stop, which would re-gate the
        // voice. One frame per voice per step, so the density matches a clean Panic.
        for (double t = 0.0; t <= 600.0; t += 30.0) {
            juce::MidiBuffer buf;
            buf.addEvent(juce::MidiMessage::createSysExMessage(frame.data() + 1,
                             static_cast<int>(frame.size()) - 2), 0);
            out.sendScheduled(buf, nowMs + t, 1000.0);
        }
    }

    static constexpr double kNoteStallMs = 180.0;  // no report for this long -> release the voice
    struct Watchdog : juce::Thread {
        AsidShared& sh;
        explicit Watchdog(AsidShared& s) : juce::Thread("SidStation note watchdog"), sh(s) {}
        void run() override {
            while (!threadShouldExit()) {
                const double now = juce::Time::getMillisecondCounterHiRes();
                for (int v = 0; v < 3; ++v)
                    if (sh.voiceNote[v].load(std::memory_order_relaxed) >= 0
                        && now - sh.voiceSeenMs[v].load(std::memory_order_relaxed) > kNoteStallMs)
                        sh.releaseStuckVoice(v, now);
                wait(40);
            }
        }
    };

    mutable juce::CriticalSection lock;
    juce::Array<Client*> clients;
    // Which SID voice each instance currently drives (for the "voice in use" marks).
    std::map<Client*, int> clientVoice;
    // Stuck-note watchdog state (see reportVoiceNote).
    std::atomic<int> voiceNote[3];
    std::atomic<double> voiceSeenMs[3];
    std::atomic<int> releaseGenV[3];
    std::atomic<double> lastPanicMs{-1.0e18};  // dedupe simultaneous transport-stop panics
    Watchdog watchdog{*this};
};
