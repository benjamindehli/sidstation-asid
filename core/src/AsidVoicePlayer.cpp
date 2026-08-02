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

Bytes AsidVoicePlayer::applyActions(const std::vector<VoiceAction>& actions) {
    std::vector<SidWrite> w;
    for (const auto& a : actions) {
        if (a.oscillator < 0 || a.oscillator > 2) continue;
        const int base = SidState::voiceBase(a.oscillator);
        if (a.gateOn) {
            sidState.setFrequency(a.oscillator, sidFrequency(a.midiNote, clockHz));
            sidState.setGate(a.oscillator, true);
            // Re-assert frequency, envelope and control so a note is reliably
            // audible even if the initial setup did not land. Control (with the
            // gate bit) sorts last in the frame, so it is written after these.
            w.push_back({static_cast<Byte>(base + 0), sidState.reg[base + 0]});  // freq lo
            w.push_back({static_cast<Byte>(base + 1), sidState.reg[base + 1]});  // freq hi
            w.push_back({static_cast<Byte>(base + 5), sidState.reg[base + 5]});  // attack/decay
            w.push_back({static_cast<Byte>(base + 6), sidState.reg[base + 6]});  // sustain/release
            w.push_back({static_cast<Byte>(base + 4), sidState.reg[base + 4]});  // control + gate
        } else {
            sidState.setGate(a.oscillator, false);
            w.push_back({static_cast<Byte>(base + 4), sidState.reg[base + 4]});
        }
    }
    return w.empty() ? Bytes{} : encodeAsidUpdate(w);
}

Bytes AsidVoicePlayer::noteOn(int channel, int midiNote, int velocity) {
    const int ch = (targetVoice >= 0) ? targetVoice : channel;  // force to target voice
    return applyActions(alloc.noteOn(ch, midiNote, velocity));
}

Bytes AsidVoicePlayer::noteOff(int channel, int midiNote) {
    const int ch = (targetVoice >= 0) ? targetVoice : channel;
    return applyActions(alloc.noteOff(ch, midiNote));
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

}  // namespace sidstation
