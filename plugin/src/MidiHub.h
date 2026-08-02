// MidiHub - direct MIDI device access for the SidStation editor.
//
// Rather than relying on the DAW to route MIDI, the plugin opens the USB-MIDI
// interface itself (chosen in the editor). This gives identical behaviour in
// any DAW and in Standalone, and is what a hardware editor/librarian needs for
// reliable patch dump/receive.
//
// Sends are made from the message thread (see the processor's drain timer).
// Incoming SysEx is decoded on JUCE's MIDI thread and handed to the listener,
// which must be thread-aware (the processor stashes it under a lock).
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>

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
    // Sends an arbitrary MIDI message (used to drain Direct-Program edits).
    void sendMessage(const juce::MidiMessage& m);
    // Sends many complete SysEx messages with `delayMs` between each, timed on a
    // background thread. The SidStation cannot receive bulk dumps at full MIDI
    // speed (per Elektron's C6 tool, 5-50 ms between packets), so patch/bank
    // transfers must be paced or they silently fail.
    void sendPaced(const std::vector<sidstation::Bytes>& messages, int delayMs);

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;

    std::unique_ptr<juce::MidiOutput> output;
    std::unique_ptr<juce::MidiInput> input;
    juce::MidiDeviceInfo outputInfo, inputInfo;
    Listener* listener = nullptr;
    juce::CriticalSection outputLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiHub)
};
