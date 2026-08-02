#include "sidstation/Parameters.h"

#include <algorithm>

namespace sidstation {
namespace {

// ---- Direct-Program base addresses (manual pages 41-42) -------------------
// The first ~0x24 bytes (name, direct controllers, filter, mode) are global.
// Oscillators are spaced 21 (0x15) bytes apart. LFOs 28 (0x1C) bytes apart.
// The manual prints DP positions as pre-split "high low" 7-bit byte pairs, so
// e.g. LFO1 Type "01 06" means position (1<<7)|0x06 = 0x86, not hex 0x106.
constexpr std::uint16_t kOscBase[3] = {0x47, 0x5C, 0x71};
constexpr std::uint16_t kLfoBase[4] = {0x86, 0xA2, 0xBE, 0xDA};

void add(std::vector<ParamInfo>& t, ParamInfo p) { t.push_back(std::move(p)); }

// Convenience builders keep the table readable.
ParamInfo cont(std::string id, std::string name, std::string group,
               std::uint16_t pos, Byte mask, Byte shift, int lo, int hi, int cc = -1) {
    return {std::move(id), std::move(name), std::move(group),
            DpAddress{pos, mask, shift}, lo, hi, ParamKind::Continuous, cc};
}
ParamInfo boolp(std::string id, std::string name, std::string group,
                std::uint16_t pos, Byte shift, int cc = -1) {
    return {std::move(id), std::move(name), std::move(group),
            DpAddress{pos, 0x01, shift}, 0, 1, ParamKind::Bool, cc};
}
ParamInfo enump(std::string id, std::string name, std::string group,
                std::uint16_t pos, Byte mask, Byte shift, int hi, int cc = -1,
                std::vector<EnumChoice> choices = {}) {
    ParamInfo p{std::move(id), std::move(name), std::move(group),
                DpAddress{pos, mask, shift}, 0, hi, ParamKind::Enum, cc};
    p.choices = std::move(choices);
    return p;
}
ParamInfo bip(std::string id, std::string name, std::string group,
              std::uint16_t pos, int lo, int hi, int cc = -1) {
    return {std::move(id), std::move(name), std::move(group),
            DpAddress{pos, 0x7F, 0}, lo, hi, ParamKind::Bipolar, cc};
}

std::vector<ParamInfo> build() {
    std::vector<ParamInfo> t;

    // Named value tables for the Enum parameters.
    const std::vector<EnumChoice> wave = {
        {1, "Triangle"}, {2, "Saw"}, {4, "Pulse"}, {5, "Mixed"}, {8, "Noise"}};
    const std::vector<EnumChoice> lfoSel = {
        {0, "LFO 1"}, {1, "LFO 2"}, {2, "LFO 3"}, {3, "LFO 4"}};
    const std::vector<EnumChoice> lfoType = {{0, "Triangle"}, {1, "Saw"},   {2, "Ramp"},
                                             {3, "Pulse"},    {4, "Random"}, {7, "Flat"}};
    const std::vector<EnumChoice> ctrlSrc = {
        {0, "Mod Wheel"}, {1, "Pitch Bend"}, {2, "Velocity"}, {3, "Aftertouch"},
        {4, "Ctrl 1"},    {5, "Ctrl 2"},     {6, "Ctrl 3"},   {7, "Ctrl 4"},
        {8, "LFO 1"},     {9, "LFO 2"},      {10, "LFO 3"},   {11, "LFO 4"}};
    const std::vector<EnumChoice> ctrlDest = {
        {0, "None"}, {1, "LFO Depth"}, {2, "LFO Speed"}, {3, "LFO S/H"}, {4, "LFO Lace"}};
    const std::vector<EnumChoice> laceWith = {
        {0, "Zero"}, {1, "LFO 1"}, {2, "LFO 2"}, {3, "LFO 3"}, {4, "LFO 4"}};
    const std::vector<EnumChoice> filterMode = {
        {0, "Off"},  {1, "Low"},       {2, "Band"},      {3, "Low+Band"},
        {4, "High"}, {5, "Low+High"},  {6, "Band+High"}, {7, "All"}};
    const std::vector<EnumChoice> filterRoute = {
        {0, "None"},    {1, "Osc 1"},   {2, "Osc 2"},    {3, "Osc 1+2"},
        {4, "Osc 3"},   {5, "Osc 1+3"}, {6, "Osc 2+3"},  {7, "All"}};

    // ---- Global: direct-controller assignments & limits (0x0A..0x15) -------
    for (int i = 0; i < 4; ++i) {
        std::uint16_t base = 0x0A + i * 3;
        std::string g = "Direct Ctrl";
        std::string n = std::to_string(i + 1);
        add(t, enump("dctrl" + n + ".assign", "Direct Ctrl " + n + " Assign", g, base, 0x7F, 0, 82));
        add(t, cont("dctrl" + n + ".limitUp", "Direct Ctrl " + n + " Limit Up", g, base + 1, 0x7F, 0, 0, 127));
        add(t, cont("dctrl" + n + ".limitDown", "Direct Ctrl " + n + " Limit Down", g, base + 2, 0x7F, 0, 0, 127));
    }

    // ---- Global: mode bitfield at 0x16 ------------------------------------
    add(t, boolp("osc1.active", "Osc 1 Active", "Global", 0x16, 0, 24));
    add(t, boolp("osc2.active", "Osc 2 Active", "Global", 0x16, 1, 25));
    add(t, boolp("osc3.active", "Osc 3 Active", "Global", 0x16, 2, 26));
    add(t, boolp("global.poly", "Poly Mode", "Global", 0x16, 3));
    add(t, boolp("filter.syncNoteOn", "Filter Sync To Note On", "Filter", 0x16, 4));
    add(t, boolp("global.legato", "Legato", "Global", 0x16, 5));
    add(t, boolp("filter.wrap", "Filter Wrap", "Filter", 0x16, 6));
    add(t, boolp("filter.envInvert", "Filter Env Invert", "Filter", 0x16, 7));

    // ---- Filter (0x18..0x23) ----------------------------------------------
    add(t, enump("filter.route", "Filter Route", "Filter", 0x18, 0x07, 0, 7, -1, filterRoute));
    add(t, cont("filter.resonance", "Filter Resonance", "Filter", 0x18, 0x0F, 4, 0, 15));
    add(t, enump("filter.mode", "Filter Mode", "Filter", 0x19, 0x07, 0, 7, -1, filterMode));
    add(t, enump("filter.lfoToCutoff", "Filter LFO Source", "Filter", 0x19, 0x03, 4, 3, -1, lfoSel));
    add(t, boolp("filter.forceReinit", "Force Note-On Re-Init", "Filter", 0x19, 6));
    add(t, cont("filter.cutoff", "Filter Cutoff", "Filter", 0x1A, 0x7F, 0, 0, 127, 27));
    add(t, cont("filter.envDepth", "Filter Env Depth", "Filter", 0x1B, 0x7F, 0, 0, 127, 28));
    add(t, cont("filter.envAttack", "Filter Env Attack", "Filter", 0x1C, 0x7F, 0, 0, 127, 30));
    add(t, cont("filter.envDecay", "Filter Env Decay", "Filter", 0x1D, 0x7F, 0, 0, 127, 31));
    add(t, cont("filter.envSustain", "Filter Env Sustain", "Filter", 0x1E, 0x7F, 0, 0, 127, 32));
    add(t, cont("filter.envRelease", "Filter Env Release", "Filter", 0x1F, 0x7F, 0, 0, 127, 33));
    add(t, cont("filter.lfoDepth", "Filter LFO Depth", "Filter", 0x20, 0x7F, 0, 0, 127, 29));
    add(t, cont("filter.lfoWheelDepth", "Filter LFO Wheel Depth", "Filter", 0x21, 0x7F, 0, 0, 127));
    // Manual documents 50..200, but the DP field is 7-bit, and the upper half is
    // only reachable via a full patch dump. Cap the DP-editable range at 127.
    add(t, cont("global.pitchSyncSpeed", "Pitch Sync Speed", "Global", 0x22, 0x7F, 0, 50, 127));
    add(t, cont("global.pitchSyncHCut", "Pitch Sync HardCut", "Global", 0x23, 0x0F, 0, 0, 15));

    // ---- Oscillators 1..3 --------------------------------------------------
    // Per-oscillator CC numbers (pages 38-39). Order matches the field list.
    struct OscCc { int arp, pitch, transpose, vibDepth, detune, porta, sync, ring,
                       pwmStart, pwmAdd, pwmLfoDepth, delay, attack, decay, sustain, release; };
    const OscCc oscCc[3] = {
        {34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49},
        {50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 70, 71},
        {72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87},
    };
    for (int o = 0; o < 3; ++o) {
        const std::uint16_t b = kOscBase[o];
        const std::string p = "osc" + std::to_string(o + 1) + ".";
        const std::string g = "Osc " + std::to_string(o + 1);
        const OscCc& c = oscCc[o];
        // +0x00 flags byte
        add(t, boolp(p + "syncPwmAdd", g + " Sync PWM-Add", g, b + 0x00, 0));
        add(t, boolp(p + "gate", g + " Envelope Gate", g, b + 0x00, 1));
        // +0x01.. value bytes
        add(t, cont(p + "pitchTrack", g + " Pitch/Track", g, b + 0x01, 0x7F, 0, 0, 99, c.pitch));
        add(t, cont(p + "arpSpeed", g + " Arp Speed", g, b + 0x02, 0x7F, 0, 0, 127, c.arp));
        add(t, bip(p + "transpose", g + " Transpose", g, b + 0x03, -24, 24, c.transpose));
        add(t, bip(p + "detune", g + " Detune", g, b + 0x04, -64, 63, c.detune));
        add(t, cont(p + "pitchBendRange", g + " Pitch Bend Range", g, b + 0x05, 0x7F, 0, 0, 24));
        add(t, cont(p + "attack", g + " Attack", g, b + 0x06, 0x0F, 0, 0, 15, c.attack));
        add(t, cont(p + "decay", g + " Decay", g, b + 0x07, 0x0F, 0, 0, 15, c.decay));
        add(t, cont(p + "sustain", g + " Sustain", g, b + 0x08, 0x0F, 0, 0, 15, c.sustain));
        add(t, cont(p + "release", g + " Release", g, b + 0x09, 0x0F, 0, 0, 15, c.release));
        add(t, cont(p + "delay", g + " Delay", g, b + 0x0A, 0x7F, 0, 0, 127, c.delay));
        add(t, cont(p + "pwmStart", g + " PWM Start", g, b + 0x0B, 0x7F, 0, 0, 127, c.pwmStart));
        add(t, cont(p + "pwmAdd", g + " PWM Add", g, b + 0x0C, 0x7F, 0, 0, 127, c.pwmAdd));
        add(t, enump(p + "pwmLfo", g + " PWM LFO", g, b + 0x0D, 0x03, 0, 3, -1, lfoSel));
        add(t, cont(p + "pwmLfoDepth", g + " PWM LFO Depth", g, b + 0x0E, 0x7F, 0, 0, 127, c.pwmLfoDepth));
        // +0x0F OSC_WAVE byte: waveform in high nibble, SYNC=bit1, RINGM=bit2.
        add(t, enump(p + "waveform", g + " Waveform", g, b + 0x0F, 0x0F, 4, 15, -1, wave));
        add(t, boolp(p + "sync", g + " Sync", g, b + 0x0F, 1, c.sync));
        add(t, boolp(p + "ringMod", g + " Ring Mod", g, b + 0x0F, 2, c.ring));
        add(t, cont(p + "portamento", g + " Portamento", g, b + 0x10, 0x7F, 0, 0, 99, c.porta));
        add(t, enump(p + "vibratoLfo", g + " Vibrato LFO", g, b + 0x11, 0x03, 0, 3, -1, lfoSel));
        add(t, cont(p + "vibratoDepth", g + " Vibrato Depth", g, b + 0x12, 0x7F, 0, 0, 127, c.vibDepth));
        add(t, cont(p + "vibratoWheelDepth", g + " Vibrato Wheel Depth", g, b + 0x13, 0x7F, 0, 0, 127));
        add(t, cont(p + "tableSpeed", g + " Table Speed", g, b + 0x14, 0x7F, 0, 0, 127));
    }

    // ---- LFOs 1..4 ---------------------------------------------------------
    struct LfoCc { int speed, sampHold, depth, lace, addDepth, fadeIn; };
    const LfoCc lfoCc[4] = {
        {88, 92, 89, 93, 90, 91},
        {94, 104, 95, 105, 102, 103},
        {106, 110, 107, 111, 108, 109},
        {112, 116, 113, 117, 114, 115},
    };
    for (int l = 0; l < 4; ++l) {
        const std::uint16_t b = kLfoBase[l];
        const std::string p = "lfo" + std::to_string(l + 1) + ".";
        const std::string g = "LFO " + std::to_string(l + 1);
        const LfoCc& c = lfoCc[l];
        // +0x00 type/source byte
        add(t, enump(p + "type", g + " Type", g, b + 0x00, 0x07, 0, 7, -1, lfoType));
        add(t, enump(p + "ctrlSource", g + " Ctrl Source", g, b + 0x00, 0x0F, 4, 11, -1, ctrlSrc));
        // +0x01 options byte
        add(t, boolp(p + "syncNoteOn", g + " Sync Note On", g, b + 0x01, 0));
        add(t, boolp(p + "invert", g + " Invert", g, b + 0x01, 1));
        add(t, boolp(p + "aboveZero", g + " Above Zero", g, b + 0x01, 2));
        add(t, boolp(p + "syncNoteOff", g + " Sync Note Off", g, b + 0x01, 3));
        add(t, enump(p + "ctrlDest", g + " Ctrl Dest", g, b + 0x01, 0x07, 4, 4, -1, ctrlDest));
        // +0x02.. value bytes
        add(t, cont(p + "speed", g + " Speed", g, b + 0x02, 0x7F, 0, 0, 127, c.speed));
        add(t, cont(p + "sampHold", g + " Sample & Hold", g, b + 0x03, 0x7F, 0, 0, 127, c.sampHold));
        add(t, cont(p + "depth", g + " Depth", g, b + 0x04, 0x7F, 0, 0, 127, c.depth));
        add(t, enump(p + "addLfo", g + " Add LFO", g, b + 0x05, 0x03, 0, 3, -1, lfoSel));
        add(t, cont(p + "laceSpeed", g + " Lace Speed", g, b + 0x06, 0x7F, 0, 0, 127, c.lace));
        add(t, enump(p + "laceWith", g + " Lace With", g, b + 0x07, 0x07, 0, 4, -1, laceWith));
        add(t, cont(p + "addDepth", g + " Add Depth", g, b + 0x08, 0x7F, 0, 0, 127, c.addDepth));
        add(t, cont(p + "ctrlValue", g + " Ctrl Value", g, b + 0x09, 0x7F, 0, 0, 127));
        add(t, cont(p + "fadeIn", g + " Fade In", g, b + 0x0A, 0x7F, 0, 0, 127, c.fadeIn));
    }

    return t;
}

}  // namespace

const std::vector<ParamInfo>& parameters() {
    static const std::vector<ParamInfo> table = build();
    return table;
}

const ParamInfo* findParamById(const std::string& id) {
    const auto& t = parameters();
    auto it = std::find_if(t.begin(), t.end(), [&](const ParamInfo& p) { return p.id == id; });
    return it == t.end() ? nullptr : &*it;
}

const ParamInfo* findParamByCc(int cc) {
    if (cc < 0) return nullptr;
    const auto& t = parameters();
    auto it = std::find_if(t.begin(), t.end(), [&](const ParamInfo& p) { return p.cc == cc; });
    return it == t.end() ? nullptr : &*it;
}

Byte encodeParamValue(const ParamInfo& p, int value) {
    int v = std::clamp(value, p.minValue, p.maxValue);
    if (p.kind == ParamKind::Bipolar) {
        // 7-bit two's complement: -64..63 -> 0x00..0x7F
        return static_cast<Byte>(v & 0x7F);
    }
    return static_cast<Byte>(v & p.dp.mask);
}

int decodeParamValue(const ParamInfo& p, Byte data) {
    if (p.kind == ParamKind::Bipolar) {
        int v = data & 0x7F;
        if (v >= 64) v -= 128;  // sign-extend from 7 bits
        return std::clamp(v, p.minValue, p.maxValue);
    }
    return std::clamp(static_cast<int>(data & p.dp.mask), p.minValue, p.maxValue);
}

}  // namespace sidstation
