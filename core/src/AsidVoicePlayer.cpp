#include "sidstation/AsidVoicePlayer.h"

namespace sidstation {

void AsidVoicePlayer::reset() {
    alloc.reset();
    sidState = SidState{};
    for (int v = 0; v < 3; ++v) {
        sidState.setWaveform(v, sid::kSaw);       // clear tone, no pulse-width dependency
        sidState.setPulseWidth(v, 0x800);         // 50% in case the waveform is changed to pulse
        sidState.setAttackDecay(v, 0, 0);         // fast on
        sidState.setSustainRelease(v, 15, 0);     // hold at full level, fast off
    }
    sidState.setCutoff(2047);                     // filter wide open
    sidState.setResonanceRouting(0, filterRouting);
    sidState.setModeVolume(0, 15);                // full volume, no filter mode
}

std::vector<Bytes> AsidVoicePlayer::start() {
    // Do NOT send the ASID start command (F0 2D 4C). The user enters ASID from
    // the front panel, and this way one instance's start does not disturb the
    // other voices. Send this voice's setup (or all three) plus the shared
    // filter and volume registers, as a partial update.
    std::vector<SidWrite> w;
    auto addVoice = [&](int v) {
        const int b = SidState::voiceBase(v);
        for (int r = 0; r < 7; ++r)
            w.push_back({static_cast<Byte>(b + r), sidState.reg[b + r]});
    };
    if (targetVoice >= 0 && targetVoice <= 2)
        addVoice(targetVoice);
    else
        for (int v = 0; v < 3; ++v) addVoice(v);
    for (Byte r = 0x15; r <= 0x18; ++r) w.push_back({r, sidState.reg[r]});  // filter + volume
    return {encodeAsidUpdate(w)};
}

std::vector<Bytes> AsidVoicePlayer::stop() {
    // Only silence the voices. Do NOT send the ASID stop command (F0 2D 4D):
    // on OS 1.11 R34 it puts the unit in a bad state (blank screen, needs a
    // power cycle). The user exits ASID mode from the front panel instead.
    std::vector<SidWrite> w;
    for (int v = 0; v < 3; ++v) {
        sidState.setGate(v, false);
        w.push_back({static_cast<Byte>(SidState::voiceBase(v) + 4), sidState.control(v)});
    }
    return {encodeAsidUpdate(w)};
}

Bytes AsidVoicePlayer::frameForAction(const VoiceAction& a) {
    if (a.oscillator < 0 || a.oscillator > 2) return {};
    const int base = SidState::voiceBase(a.oscillator);
    std::vector<SidWrite> w;
    if (a.gateOn) {
        currentNote[a.oscillator] = a.midiNote;  // remember for pitch modulation
        sidState.setFrequency(a.oscillator, sidFrequency(a.midiNote, clockHz));
        sidState.setGate(a.oscillator, true);
        const Byte ctrl = sidState.control(a.oscillator);
        const Byte gateHigh = static_cast<Byte>(ctrl | sid::kGate);
        const Byte gateLow = static_cast<Byte>(ctrl & ~sid::kGate);
        // Frequency and envelope, then a control double-write (gate low then high)
        // that retriggers the SID envelope atomically inside this one frame.
        w.push_back({static_cast<Byte>(base + 0), sidState.reg[base + 0]});
        w.push_back({static_cast<Byte>(base + 1), sidState.reg[base + 1]});
        w.push_back({static_cast<Byte>(base + 5), sidState.reg[base + 5]});
        w.push_back({static_cast<Byte>(base + 6), sidState.reg[base + 6]});
        return encodeAsidDoubleControl(a.oscillator, gateLow, gateHigh, w);
    }
    // Release through the same double-control path (gate low in both slots) so it
    // is applied as reliably as the note-on, even under a heavy pitch stream.
    currentNote[a.oscillator] = -1;  // nothing sounding, no pitch mod
    const Byte gateLow = static_cast<Byte>(sidState.control(a.oscillator) & ~sid::kGate);
    sidState.setGate(a.oscillator, false);
    return encodeAsidDoubleControl(a.oscillator, gateLow, gateLow, {});
}

std::vector<Bytes> AsidVoicePlayer::frameSequence(const std::vector<VoiceAction>& actions) {
    std::vector<Bytes> frames;
    for (const auto& a : actions) {
        Bytes f = frameForAction(a);
        if (f.empty()) continue;
        frames.push_back(f);
        if (a.oscillator < 0 || a.oscillator > 2) continue;
        // The unit applies each write only when the next message arrives, so add
        // a flush: re-write the control register at its current value. After an
        // attack that is gate high (no second edge, so no double retrigger); after
        // a release it is a second gate-off. Either way the main frame applies.
        const int base = SidState::voiceBase(a.oscillator);
        frames.push_back(encodeAsidUpdate({{static_cast<Byte>(base + 4), sidState.reg[base + 4]}}));
    }
    return frames;
}

Bytes AsidVoicePlayer::settleFrame(int voice) const {
    if (voice < 0 || voice > 2) return {};
    const int base = SidState::voiceBase(voice);
    return encodeAsidUpdate({{static_cast<Byte>(base + 6), sidState.reg[base + 6]}});
}

std::vector<Bytes> AsidVoicePlayer::hardRestartDrain(int voice) {
    if (voice < 0 || voice > 2) return {};
    const int base = SidState::voiceBase(voice);
    const Byte srDrain = static_cast<Byte>(sidState.reg[base + 6] & 0xF0);   // keep sustain, release -> 0
    const Byte ctrlOff = static_cast<Byte>(sidState.control(voice) & ~sid::kGate);  // gate off
    // Built without mutating state: the following note-on rewrites the real SR
    // and gate. Frame plus a flush so the unit applies it.
    return {encodeAsidUpdate({{static_cast<Byte>(base + 6), srDrain},
                              {static_cast<Byte>(base + 4), ctrlOff}}),
            encodeAsidUpdate({{static_cast<Byte>(base + 4), ctrlOff}})};
}

std::vector<Bytes> AsidVoicePlayer::noteOn(int channel, int midiNote, int velocity) {
    const int ch = (targetVoice >= 0) ? targetVoice : channel;
    return frameSequence(alloc.noteOn(ch, midiNote, velocity));
}

std::vector<Bytes> AsidVoicePlayer::noteOff(int channel, int midiNote) {
    const int ch = (targetVoice >= 0) ? targetVoice : channel;
    return frameSequence(alloc.noteOff(ch, midiNote));
}

Bytes AsidVoicePlayer::setVolume(int vol0to15) {
    const Byte vol = static_cast<Byte>(vol0to15 < 0 ? 0 : (vol0to15 > 15 ? 15 : vol0to15));
    sidState.setModeVolume(sidState.reg[24] & 0xF0, vol);  // keep filter mode bits
    return encodeAsidUpdate({{0x18, sidState.reg[24]}});
}

Bytes AsidVoicePlayer::setCutoff(int cutoff0to2047) {
    const int c = cutoff0to2047 < 0 ? 0 : (cutoff0to2047 > 2047 ? 2047 : cutoff0to2047);
    sidState.setCutoff(static_cast<std::uint16_t>(c));
    return encodeAsidUpdate({{0x15, sidState.reg[21]}, {0x16, sidState.reg[22]}});
}

Bytes AsidVoicePlayer::setResonance(int res0to15) {
    const Byte res = static_cast<Byte>(res0to15 < 0 ? 0 : (res0to15 > 15 ? 15 : res0to15));
    sidState.setResonanceRouting(res, filterRouting);
    return encodeAsidUpdate({{0x17, sidState.reg[23]}});
}

Bytes AsidVoicePlayer::setFilterMode(Byte modeBits) {
    sidState.setModeVolume(modeBits, sidState.reg[24] & 0x0F);  // keep volume
    return encodeAsidUpdate({{0x18, sidState.reg[24]}});
}

Bytes AsidVoicePlayer::setWaveform(int voice, Byte waveBits) {
    if (voice < 0 || voice > 2) return {};
    sidState.setWaveform(voice, waveBits);
    const int base = SidState::voiceBase(voice);
    return encodeAsidUpdate({{static_cast<Byte>(base + 4), sidState.reg[base + 4]}});
}

Bytes AsidVoicePlayer::setPulseWidth(int voice, int pw0to4095) {
    if (voice < 0 || voice > 2) return {};
    const int pw = pw0to4095 < 0 ? 0 : (pw0to4095 > 4095 ? 4095 : pw0to4095);
    sidState.setPulseWidth(voice, static_cast<std::uint16_t>(pw));
    const int base = SidState::voiceBase(voice);
    return encodeAsidUpdate({{static_cast<Byte>(base + 2), sidState.reg[base + 2]},
                             {static_cast<Byte>(base + 3), sidState.reg[base + 3]}});
}

static Byte clampNibble(int v) { return static_cast<Byte>(v < 0 ? 0 : (v > 15 ? 15 : v)); }

Bytes AsidVoicePlayer::setAttackDecay(int voice, int attack0to15, int decay0to15) {
    if (voice < 0 || voice > 2) return {};
    sidState.setAttackDecay(voice, clampNibble(attack0to15), clampNibble(decay0to15));
    const int base = SidState::voiceBase(voice);
    return encodeAsidUpdate({{static_cast<Byte>(base + 5), sidState.reg[base + 5]}});
}

Bytes AsidVoicePlayer::setSustainRelease(int voice, int sustain0to15, int release0to15) {
    if (voice < 0 || voice > 2) return {};
    sidState.setSustainRelease(voice, clampNibble(sustain0to15), clampNibble(release0to15));
    const int base = SidState::voiceBase(voice);
    return encodeAsidUpdate({{static_cast<Byte>(base + 6), sidState.reg[base + 6]}});
}

Bytes AsidVoicePlayer::setFilterRouting(int voice, bool routeThroughFilter) {
    if (voice < 0 || voice > 2) return {};
    const Byte bit = voice == 0 ? sid::kFilt1 : (voice == 1 ? sid::kFilt2 : sid::kFilt3);
    filterRouting = routeThroughFilter ? static_cast<Byte>(filterRouting | bit)
                                       : static_cast<Byte>(filterRouting & ~bit);
    const Byte resonance = static_cast<Byte>(sidState.reg[23] >> 4);
    sidState.setResonanceRouting(resonance, filterRouting);
    return encodeAsidUpdate({{0x17, sidState.reg[23]}});
}

// Sets or clears a control-register bit while keeping the waveform and gate.
static Bytes setControlBit(SidState& s, int voice, Byte bit, bool on) {
    if (voice < 0 || voice > 2) return {};
    const int base = SidState::voiceBase(voice);
    Byte c = s.control(voice);
    c = on ? static_cast<Byte>(c | bit) : static_cast<Byte>(c & ~bit);
    s.setControl(voice, c);
    return encodeAsidUpdate({{static_cast<Byte>(base + 4), s.reg[base + 4]}});
}

Bytes AsidVoicePlayer::setResonanceRouting(int res0to15, int routingBits0to7) {
    filterRouting = static_cast<Byte>(routingBits0to7 & 0x0F);
    sidState.setResonanceRouting(clampNibble(res0to15), filterRouting);
    return encodeAsidUpdate({{0x17, sidState.reg[23]}});
}

Bytes AsidVoicePlayer::setPitchMod(int voice, double semitones) {
    if (voice < 0 || voice > 2 || currentNote[voice] < 0) return {};
    sidState.setFrequency(voice, sidFrequency(currentNote[voice] + semitones, clockHz));
    const int base = SidState::voiceBase(voice);
    return encodeAsidUpdate({{static_cast<Byte>(base + 0), sidState.reg[base + 0]},
                             {static_cast<Byte>(base + 1), sidState.reg[base + 1]}});
}

Bytes AsidVoicePlayer::setSync(int voice, bool on) {
    return setControlBit(sidState, voice, sid::kSync, on);
}

Bytes AsidVoicePlayer::setRing(int voice, bool on) {
    return setControlBit(sidState, voice, sid::kRing, on);
}

}  // namespace sidstation
