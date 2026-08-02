#include "sidstation/VoiceEngine.h"

#include <algorithm>

namespace sidstation {

int sidNoteFromMidi(int midiNote, int offset) {
    int n = midiNote - offset;
    if (n < 1) n = 1;
    if (n > 99) n = 99;
    return n;
}

int VoiceEngine::oscForChannel(int channel) const {
    switch (mode) {
        case VoiceMode::PerChannel:
            return (channel >= 0 && channel < 3) ? channel : -1;
    }
    return -1;
}

std::vector<VoiceAction> VoiceEngine::noteOn(int channel, int midiNote, int velocity) {
    const int osc = oscForChannel(channel);
    if (osc < 0) return {};

    auto& stack = held[static_cast<size_t>(osc)];
    stack.erase(std::remove(stack.begin(), stack.end(), midiNote), stack.end());
    stack.push_back(midiNote);

    VoiceAction a;
    a.oscillator = osc;
    a.gateOn = true;
    a.sidNote = sidNoteFromMidi(midiNote, offset);
    a.midiNote = midiNote;
    a.velocity = velocity;
    return {a};
}

std::vector<VoiceAction> VoiceEngine::noteOff(int channel, int midiNote) {
    const int osc = oscForChannel(channel);
    if (osc < 0) return {};

    auto& stack = held[static_cast<size_t>(osc)];
    if (stack.empty()) return {};

    const bool wasActive = (stack.back() == midiNote);
    stack.erase(std::remove(stack.begin(), stack.end(), midiNote), stack.end());

    // Releasing an older, non sounding note changes nothing audible.
    if (!wasActive) return {};

    VoiceAction a;
    a.oscillator = osc;
    a.midiNote = midiNote;
    if (stack.empty()) {
        a.gateOn = false;  // nothing left on this oscillator, release it
        return {a};
    }
    // Fall back to the most recently held note that is still down (legato).
    const int nt = stack.back();
    a.gateOn = true;
    a.sidNote = sidNoteFromMidi(nt, offset);
    a.midiNote = nt;
    return {a};
}

std::vector<VoiceAction> VoiceEngine::allNotesOff() {
    std::vector<VoiceAction> out;
    for (int osc = 0; osc < 3; ++osc) {
        if (held[static_cast<size_t>(osc)].empty()) continue;
        held[static_cast<size_t>(osc)].clear();
        VoiceAction a;
        a.oscillator = osc;
        a.gateOn = false;
        out.push_back(a);
    }
    return out;
}

void VoiceEngine::reset() {
    for (auto& s : held) s.clear();
}

}  // namespace sidstation
