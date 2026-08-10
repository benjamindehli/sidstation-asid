// Minimal dependency-free test harness for the SidStation core protocol lib.
// Cross-checks encoders against literal byte sequences documented in the
// SidStation Owners Manual (r22b, OS1.1), pages 39-43.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "sidstation/ControllerMap.h"
#include "sidstation/DirectProgram.h"
#include "sidstation/Parameters.h"
#include "sidstation/Patch.h"
#include "sidstation/Asid.h"
#include "sidstation/AsidVoicePlayer.h"
#include "sidstation/Lfo.h"
#include "sidstation/WaveTable.h"
#include "sidstation/SysExStream.h"
#include "sidstation/SyxFile.h"
#include "sidstation/VoiceEngine.h"

using namespace sidstation;

static int g_failures = 0;
static int g_checks   = 0;

static std::string hex(const Bytes& b) {
    std::string s;
    char buf[4];
    for (Byte x : b) { std::snprintf(buf, sizeof buf, "%02X ", x); s += buf; }
    if (!s.empty()) s.pop_back();
    return s;
}

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_failures;                                                   \
            std::printf("FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__);    \
        }                                                                   \
    } while (0)

static void checkBytes(const char* what, const Bytes& got, const Bytes& want) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s\n  got:  %s\n  want: %s\n", what, hex(got).c_str(), hex(want).c_str());
    }
}

// Builds the expected 12-byte DP message from the manual's "posHi posLo mask shift data" pair.
static Bytes dp(Byte pHi, Byte pLo, Byte mask, Byte shift, Byte data) {
    return {0xF0, 0x00, 0x20, 0x3C, 0x01, 0x00, 0x04, pHi, pLo, mask, shift, data, 0xF7};
}

static void testDirectProgramFraming() {
    // Manual example (page 43): "DP: 01 76 0f 04 08" - first table position = Noise.
    DpAddress tablePos{static_cast<std::uint16_t>((0x01 << 7) | 0x76), 0x0F, 0x04};
    checkBytes("DP framing table1/Noise", encodeDirectProgram(tablePos, 0x08),
               dp(0x01, 0x76, 0x0F, 0x04, 0x08));

    // Round-trip decode.
    auto d = decodeDirectProgram(encodeDirectProgram(tablePos, 0x08));
    CHECK(d.valid, "DP decode valid");
    CHECK(d.address.position == tablePos.position, "DP decode position");
    CHECK(d.address.mask == 0x0F && d.address.shift == 0x04, "DP decode mask/shift");
    CHECK(d.data == 0x08, "DP decode data");

    // Bad length must not parse.
    CHECK(!decodeDirectProgram({0xF0, 0xF7}).valid, "DP reject short");
}

// Each entry: parameter id, and the manual's documented "posHi posLo mask shift".
struct Known { const char* id; Byte pHi, pLo, mask, shift; };

static void testParamAddresses() {
    const Known known[] = {
        // Global / filter (pages 41-42)
        {"osc1.active",      0x00, 0x16, 0x01, 0x00},
        {"osc2.active",      0x00, 0x16, 0x01, 0x01},
        {"osc3.active",      0x00, 0x16, 0x01, 0x02},
        {"global.poly",      0x00, 0x16, 0x01, 0x03},
        {"filter.envInvert", 0x00, 0x16, 0x01, 0x07},
        {"filter.route",     0x00, 0x18, 0x07, 0x00},
        {"filter.resonance", 0x00, 0x18, 0x0F, 0x04},
        {"filter.mode",      0x00, 0x19, 0x07, 0x00},
        {"filter.cutoff",    0x00, 0x1A, 0x7F, 0x00},
        {"filter.envRelease",0x00, 0x1F, 0x7F, 0x00},
        {"global.pitchSyncHCut", 0x00, 0x23, 0x0F, 0x00},
        // Oscillator 1 (page 42)
        {"osc1.gate",        0x00, 0x47, 0x01, 0x01},
        {"osc1.pitchTrack",  0x00, 0x48, 0x7F, 0x00},
        {"osc1.attack",      0x00, 0x4D, 0x0F, 0x00},
        {"osc1.waveform",    0x00, 0x56, 0x0F, 0x04},
        {"osc1.tableSpeed",  0x00, 0x5B, 0x7F, 0x00},
        // Oscillator 2 / 3 boundaries
        {"osc2.waveform",    0x00, 0x6B, 0x0F, 0x04},
        {"osc3.waveform",    0x01, 0x00, 0x0F, 0x04},
        {"osc3.tableSpeed",  0x01, 0x05, 0x7F, 0x00},
        // LFOs (page 42) - these confirm the high/low split handling
        {"lfo1.type",        0x01, 0x06, 0x07, 0x00},
        {"lfo1.ctrlSource",  0x01, 0x06, 0x0F, 0x04},
        {"lfo1.speed",       0x01, 0x08, 0x7F, 0x00},
        {"lfo1.fadeIn",      0x01, 0x10, 0x7F, 0x00},
        {"lfo2.type",        0x01, 0x22, 0x07, 0x00},
        {"lfo2.depth",       0x01, 0x26, 0x7F, 0x00},
        {"lfo3.type",        0x01, 0x3E, 0x07, 0x00},
        {"lfo4.type",        0x01, 0x5A, 0x07, 0x00},
        {"lfo4.fadeIn",      0x01, 0x64, 0x7F, 0x00},
    };
    for (const auto& k : known) {
        const ParamInfo* p = findParamById(k.id);
        if (!p) { ++g_checks; ++g_failures; std::printf("FAIL: missing param %s\n", k.id); continue; }
        Byte data = encodeParamValue(*p, p->minValue);
        checkBytes((std::string("DP addr ") + k.id).c_str(),
                   encodeDirectProgram(p->dp, data), dp(k.pHi, k.pLo, k.mask, k.shift, data));
    }
}

static void testBipolarEncoding() {
    const ParamInfo* tr = findParamById("osc1.transpose");
    CHECK(tr != nullptr, "transpose exists");
    CHECK(encodeParamValue(*tr, 0) == 0x00, "transpose 0");
    CHECK(encodeParamValue(*tr, 24) == 24, "transpose +24");
    CHECK(encodeParamValue(*tr, -1) == 0x7F, "transpose -1 -> 0x7F");
    CHECK(encodeParamValue(*tr, -24) == static_cast<Byte>(-24 & 0x7F), "transpose -24");
    CHECK(decodeParamValue(*tr, 0x7F) == -1, "decode 0x7F -> -1");

    const ParamInfo* dt = findParamById("osc1.detune");
    CHECK(decodeParamValue(*dt, encodeParamValue(*dt, -64)) == -64, "detune -64 round-trip");
    CHECK(decodeParamValue(*dt, encodeParamValue(*dt, 63)) == 63, "detune +63 round-trip");
}

static void testCcMap() {
    // Spot-check CC assignments from pages 38-39.
    CHECK(findParamByCc(27) && findParamByCc(27)->id == "filter.cutoff", "CC27 -> filter cutoff");
    CHECK(findParamByCc(24) && findParamByCc(24)->id == "osc1.active", "CC24 -> osc1 active");
    CHECK(findParamByCc(70) && findParamByCc(70)->id == "osc2.sustain", "CC70 -> osc2 sustain");
    CHECK(findParamByCc(102) && findParamByCc(102)->id == "lfo2.addDepth", "CC102 -> lfo2 add depth");
    CHECK(findParamByCc(117) && findParamByCc(117)->id == "lfo4.laceSpeed", "CC117 -> lfo4 lace speed");

    // Building a CC message from a param+value. A 0..127 param passes through
    // unchanged (the verified cutoff case).
    const ParamInfo* cutoff = findParamById("filter.cutoff");
    checkBytes("CC message cutoff=64", controlChange(0, *cutoff, 64), Bytes{0xB0, 27, 64});
    CHECK(ccValue(*cutoff, 42) == 42, "cutoff 42 -> CC 42 (0..127 pass-through)");

    // Booleans send 0 or 127, not the raw 0/1.
    const ParamInfo* active = findParamById("osc1.active");
    CHECK(ccValue(*active, 1) == 127 && ccValue(*active, 0) == 0, "bool -> 0/127");

    // A sub-range param scales up onto 0..127.
    const ParamInfo* attack = findParamById("osc1.attack");  // range 0..15
    CHECK(ccValue(*attack, 15) == 127 && ccValue(*attack, 0) == 0, "0..15 scales to 0..127");

    // Bipolar centres near 64.
    const ParamInfo* tr = findParamById("osc1.transpose");  // range -24..24
    CHECK(ccValue(*tr, 0) == 64, "transpose 0 -> CC 64 (centred)");
    CHECK(ccValue(*tr, 24) == 127 && ccValue(*tr, -24) == 0, "transpose extremes map to 0/127");
}

static void testPatchRoundTrip() {
    Patch p;
    p.data.resize(143, 0x00);
    p.setName("Bass Lead");
    for (std::size_t i = 10; i < p.data.size(); ++i)
        p.data[i] = static_cast<Byte>((i * 7 + 3) & 0xFF);  // arbitrary but full 8-bit values

    Bytes wire = encodePatchDump(p);
    CHECK(wire.front() == 0xF0 && wire.back() == 0xF7, "patch dump framed");
    CHECK(sysex::hasValidInit(wire), "patch dump init");

    auto decoded = decodePatchDump(wire);
    CHECK(decoded.has_value(), "patch dump decodes");
    if (decoded) {
        CHECK(decoded->name() == "Bass Lead", "patch name round-trip");
        CHECK(decoded->data == p.data, "patch data round-trip");
    }

    // Clear / skip framing.
    CHECK(encodePatchAllClear().back() == 0xF7, "all-clear framed");
    CHECK(encodeSkipPatch() == (Bytes{0xF0, 0x00, 0x20, 0x3C, 0x01, 0x00, 0x03, 0xF7}), "skip patch bytes");
}

static void testRegistrySanity() {
    const auto& t = parameters();
    CHECK(t.size() > 120, "registry has full parameter set");
    for (const auto& p : t) {
        CHECK(p.dp.mask != 0, (std::string("mask nonzero: ") + p.id).c_str());
        CHECK(p.maxValue >= p.minValue, (std::string("range order: ") + p.id).c_str());
        // Every field must fit within its mask width once shifted.
        int fieldMax = (p.kind == ParamKind::Bipolar) ? 0x7F : p.maxValue;
        CHECK((fieldMax & p.dp.mask) == fieldMax || p.kind == ParamKind::Bipolar,
              (std::string("fits mask: ") + p.id).c_str());
    }
}

static Patch makePatch(const std::string& name, std::size_t dataBytes = 143) {
    Patch p;
    p.data.resize(dataBytes, 0x00);
    p.setName(name);
    for (std::size_t i = Patch::kNameLength; i < p.data.size(); ++i)
        p.data[i] = static_cast<Byte>((i * 5 + 1) & 0xFF);
    return p;
}

static void testEnumChoices() {
    const ParamInfo* wave = findParamById("osc1.waveform");
    CHECK(wave && wave->choices.size() == 5, "waveform has 5 choices");
    if (wave) {
        // Non-contiguous device values must be preserved (Noise == 8).
        CHECK(wave->choices.back().value == 8 && wave->choices.back().label == "Noise",
              "waveform last choice is Noise=8");
    }
    const ParamInfo* src = findParamById("lfo1.ctrlSource");
    CHECK(src && src->choices.size() == 12, "ctrl source has 12 choices");

    // Continuous params carry no choices.
    const ParamInfo* cutoff = findParamById("filter.cutoff");
    CHECK(cutoff && cutoff->choices.empty(), "continuous param has no choices");
}

static void testSysExAssembler() {
    // Two messages, with system-realtime bytes interleaved (0xFE between them,
    // and a 0xF8 injected *inside* the second message's SysEx).
    Bytes msg1 = encodeDirectProgram(DpAddress{0x1A, 0x7F, 0}, 0x40);
    Bytes msg2 = encodePatchDump(makePatch("Stream", 30));

    Bytes withRealtime = msg2;
    withRealtime.insert(withRealtime.begin() + 3, 0xF8);  // clock byte mid-SysEx

    Bytes stream = msg1;
    stream.push_back(0xFE);  // active sensing between messages
    stream.insert(stream.end(), withRealtime.begin(), withRealtime.end());

    SysExAssembler a;
    auto whole = a.feed(stream);
    ++g_checks;
    if (whole.size() != 2) { ++g_failures; std::printf("FAIL: assembler count %zu\n", whole.size()); }
    else {
        checkBytes("assembler msg1", whole[0], msg1);
        checkBytes("assembler msg2 (realtime stripped)", whole[1], msg2);
    }

    // Same stream fed in two chunks: a SysEx split across feeds must survive.
    SysExAssembler b;
    std::vector<Bytes> acc;
    std::size_t cut = msg1.size() + 5;  // mid-way through msg2
    for (auto& m : b.feed(stream.data(), cut)) acc.push_back(m);
    for (auto& m : b.feed(stream.data() + cut, stream.size() - cut)) acc.push_back(m);
    CHECK(acc.size() == 2, "assembler across chunks: 2 messages");
    if (acc.size() == 2) checkBytes("chunked msg2", acc[1], msg2);
}

static void testSyxAndLibrary() {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "sidstation_libtest";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // Single-patch .syx round-trip.
    Patch alpha = makePatch("Alpha");
    std::string aPath = (dir / "a.syx").string();
    CHECK(savePatchToFile(aPath, alpha), "savePatchToFile");
    auto loaded = loadPatchFromFile(aPath);
    CHECK(loaded.has_value(), "loadPatchFromFile");
    if (loaded) {
        CHECK(loaded->name() == "Alpha", "syx name round-trip");
        CHECK(loaded->data == alpha.data, "syx data round-trip");
    }

    // Bulk stream: two dumps + a skip -> extractPatches finds exactly two.
    Bytes bulk = encodePatchDump(alpha);
    Bytes skip = encodeSkipPatch();
    bulk.insert(bulk.end(), skip.begin(), skip.end());
    Bytes beta = encodePatchDump(makePatch("Beta"));
    bulk.insert(bulk.end(), beta.begin(), beta.end());
    auto extracted = extractPatches(bulk);
    CHECK(extracted.size() == 2, "extractPatches count (skip ignored)");
    if (extracted.size() == 2) {
        CHECK(extracted[0].name() == "Alpha", "bulk patch 0 name");
        CHECK(extracted[1].name() == "Beta", "bulk patch 1 name");
    }

    // extractPatchItems: same split, but each keeps its exact single-dump bytes.
    auto items = extractPatchItems(bulk);
    CHECK(items.size() == 2, "extractPatchItems count");
    if (items.size() == 2) {
        CHECK(items[0].name == "Alpha" && items[1].name == "Beta", "item names");
        // Each item's message is a standalone patch dump that decodes on its own.
        auto solo = decodePatchDump(items[1].message);
        CHECK(solo && solo->name() == "Beta", "item message is a valid single dump");
        CHECK(items[0].message == encodePatchDump(alpha), "item message bytes exact");
    }

    // Folder scan: two .syx + a non-syx file that must be ignored.
    savePatchToFile((dir / "b.syx").string(), makePatch("Beta"));
    writeSyxFile((dir / "notes.txt").string(), Bytes{'x'});
    auto entries = scanPatchFolder(dir.string());
    CHECK(entries.size() == 2, "scanPatchFolder ignores non-syx");
    if (entries.size() == 2) {
        CHECK(entries[0].valid && entries[1].valid, "scan entries valid");
        CHECK(entries[0].name == "Alpha", "scan entry 0 name (sorted)");
        CHECK(entries[1].name == "Beta", "scan entry 1 name");
    }

    // Missing file / bad path behaves gracefully.
    CHECK(!readSyxFile((dir / "nope.syx").string()).has_value(), "readSyxFile missing");
    CHECK(!loadPatchFromFile((dir / "notes.txt").string()).has_value(), "load non-patch");

    fs::remove_all(dir, ec);
}

static void testVoiceEngine() {
    // Note to SID pitch conversion, with clamping at both ends.
    CHECK(sidNoteFromMidi(60) == 48, "midi 60 -> sid 48 (offset 12)");
    CHECK(sidNoteFromMidi(0) == 1, "low note clamps to 1");
    CHECK(sidNoteFromMidi(127) == 99, "high note clamps to 99");
    CHECK(sidNoteFromMidi(60, 0) == 60, "offset 0 passes through");

    VoiceEngine e;

    // Per channel routing: channels 0, 1, 2 map to oscillators 0, 1, 2.
    auto a1 = e.noteOn(0, 60, 100);
    CHECK(a1.size() == 1 && a1[0].oscillator == 0 && a1[0].gateOn &&
              a1[0].sidNote == 48,
          "ch0 note -> osc0 gate on");
    CHECK(a1[0].retrigger, "a fresh note re-attacks the envelope");
    auto a2 = e.noteOn(1, 64, 100);
    CHECK(a2.size() == 1 && a2[0].oscillator == 1, "ch1 note -> osc1");
    auto a3 = e.noteOn(2, 67, 100);
    CHECK(a3.size() == 1 && a3[0].oscillator == 2, "ch2 note -> osc2");
    CHECK(e.noteOn(3, 70, 100).empty(), "ch3 ignored (only three voices)");

    // Last note priority with legato fall back on the same oscillator.
    e.reset();
    e.noteOn(0, 60, 100);
    auto stealing = e.noteOn(0, 62, 100);
    CHECK(stealing.size() == 1 && stealing[0].sidNote == sidNoteFromMidi(62),
          "newest note takes the voice");
    CHECK(stealing[0].gateOn && !stealing[0].retrigger,
          "a legato overlap retunes without re-attacking");
    auto backTo60 = e.noteOff(0, 62);
    CHECK(backTo60.size() == 1 && backTo60[0].gateOn &&
              backTo60[0].midiNote == 60,
          "releasing top note falls back to held note");
    CHECK(!backTo60[0].retrigger, "legato fall-back retunes without re-attacking");
    auto gateOff = e.noteOff(0, 60);
    CHECK(gateOff.size() == 1 && !gateOff[0].gateOn, "last release gates off");

    // Releasing a held but non sounding note makes no sound change.
    e.reset();
    e.noteOn(0, 60, 100);
    e.noteOn(0, 62, 100);
    CHECK(e.noteOff(0, 60).empty(), "releasing older held note is silent");
    auto off62 = e.noteOff(0, 62);
    CHECK(off62.size() == 1 && !off62[0].gateOn, "releasing active note gates off");

    // allNotesOff releases every sounding oscillator.
    e.reset();
    e.noteOn(0, 60, 100);
    e.noteOn(2, 67, 100);
    auto all = e.allNotesOff();
    CHECK(all.size() == 2, "allNotesOff releases both sounding voices");
}

static void testAsid() {
    // Command messages.
    checkBytes("ASID start", encodeAsidStart(), Bytes{0xF0, 0x2D, 0x4C, 0xF7});
    checkBytes("ASID stop", encodeAsidStop(), Bytes{0xF0, 0x2D, 0x4D, 0xF7});
    checkBytes("ASID LCD", encodeAsidLcd("Hi"), Bytes{0xF0, 0x2D, 0x4F, 'H', 'i', 0xF7});

    // Slot mapping (from the spec / vap regid.h).
    CHECK(asidSlotForRegister(0x00) == 0, "reg 0 -> slot 0");
    CHECK(asidSlotForRegister(0x05) == 4, "reg 5 -> slot 4");
    CHECK(asidSlotForRegister(0x04) == 22, "voice1 control reg 0x04 -> slot 22");
    CHECK(asidSlotForRegister(0x0B) == 23, "voice2 control reg 0x0B -> slot 23");
    CHECK(asidSlotForRegister(0x12) == 24, "voice3 control reg 0x12 -> slot 24");
    CHECK(asidSlotForRegister(0x18) == 21, "reg 0x18 -> slot 21");

    // Update with two low registers (voice 1 frequency): slots 0 and 1 -> mask1 = 0x03.
    checkBytes("ASID update freq lo/hi",
               encodeAsidUpdate({{0x00, 0x34}, {0x01, 0x12}}),
               Bytes{0xF0, 0x2D, 0x4E, 0x03, 0x00, 0x00, 0x00,  // mask
                     0x00, 0x00, 0x00, 0x00,                    // msb
                     0x34, 0x12, 0xF7});                        // data

    // A value with the 8th bit set must show up in the msb byte, data carries 7 bits.
    // reg 0x16 is slot 19 -> byte 2, bit 5 (mask/msb byte index 2 = 0x20).
    checkBytes("ASID update msb bit",
               encodeAsidUpdate({{0x16, 0xC0}}),
               Bytes{0xF0, 0x2D, 0x4E, 0x00, 0x00, 0x20, 0x00,  // mask
                     0x00, 0x00, 0x20, 0x00,                    // msb
                     0x40, 0xF7});                              // data (0xC0 & 0x7F)

    // Note to SID frequency: A4 (MIDI 69) is about 7493 on the PAL clock.
    CHECK(sidFrequency(69) >= 7492 && sidFrequency(69) <= 7494, "A4 -> ~7493 SID freq");
    CHECK(sidFrequency(69 + 12) > sidFrequency(69) * 1.9, "octave up roughly doubles freq");

    // SidState helpers land in the right registers.
    SidState s;
    s.setFrequency(1, 0x1234);          // voice 2 -> regs 7,8
    s.setWaveform(0, sid::kPulse);
    s.setGate(0, true);                 // voice 1 control reg 4
    CHECK(s.reg[7] == 0x34 && s.reg[8] == 0x12, "voice2 frequency registers");
    CHECK(s.reg[4] == (sid::kPulse | sid::kGate), "voice1 control = pulse + gate");
    CHECK(s.fullUpdate().front() == 0xF0 && s.fullUpdate().back() == 0xF7, "full update framed");
}

static void testAsidPlayer() {
    AsidVoicePlayer p;
    auto s = p.start();
    CHECK(s.size() == 1, "start is a single register-setup update (no ASID start cmd)");
    CHECK(!s.empty() && s[0].front() == 0xF0 && s[0].back() == 0xF7, "start update framed");

    // Per-channel mode (default): channel 0 note drives voice 0 (control reg 4).
    auto frames = p.noteOn(0, 69, 100);
    CHECK(frames.size() == 2, "note on is the retrigger frame plus a flush");
    // The retrigger frame writes control (voice 0 -> slots 22 and 25) twice: gate
    // low then gate high. Both bits are flagged in mask byte 3 (bits 1 and 4).
    CHECK((frames[0][6] & ((1 << 1) | (1 << 4))) == ((1 << 1) | (1 << 4)),
          "note on double-writes the control register (primary + secondary slot)");
    CHECK((frames[1][6] & (1 << 4)) == 0, "flush uses only the primary control slot, no second edge");
    const std::uint16_t f = sidFrequency(69);
    CHECK(p.state().reg[0] == (f & 0xFF) && p.state().reg[1] == ((f >> 8) & 0xFF),
          "voice 0 frequency registers set from the note");
    CHECK((p.state().reg[4] & sid::kGate) != 0, "voice 0 gated on");
    auto offFrames = p.noteOff(0, 69);
    CHECK(offFrames.size() == 2, "note off is the double-control release plus a flush");
    CHECK((offFrames[0][6] & ((1 << 1) | (1 << 4))) == ((1 << 1) | (1 << 4)),
          "note off double-writes the control register (both slots, gate low)");
    CHECK((p.state().reg[4] & sid::kGate) == 0, "voice 0 gated off");

    // Target-voice mode: every note goes to the chosen voice regardless of channel.
    AsidVoicePlayer p2;
    p2.setTargetVoice(2);                        // voice 3, control register index 18
    p2.noteOn(0, 60, 100);                       // channel 0, but forced to voice 2
    CHECK((p2.state().reg[18] & sid::kGate) != 0, "target voice 2 gated on from any channel");
    CHECK((p2.state().reg[4] & sid::kGate) == 0, "voice 0 untouched in target mode");
    p2.noteOff(0, 60);
    CHECK((p2.state().reg[18] & sid::kGate) == 0, "target voice 2 gated off");

    // A global control produces a framed update and stores the value.
    CHECK(p.setVolume(10).front() == 0xF0, "setVolume update framed");
    CHECK((p.state().reg[24] & 0x0F) == 10, "volume stored in low nibble of reg 0x18");

    // Per-voice controls write the right registers.
    p.setAttackDecay(0, 3, 5);
    CHECK(p.state().reg[5] == ((3 << 4) | 5), "attack/decay nibbles in reg 5");
    p.setSustainRelease(0, 12, 2);
    CHECK(p.state().reg[6] == ((12 << 4) | 2), "sustain/release nibbles in reg 6");
    p.setPulseWidth(0, 0x0800);
    CHECK(p.state().reg[2] == 0x00 && p.state().reg[3] == 0x08, "pulse width split 12-bit");
    p.setFilterRouting(0, true);
    CHECK((p.state().reg[23] & sid::kFilt1) != 0, "voice 0 routed through filter");
    // Whole resonance+routing register at once (shared routing bits).
    p.setResonanceRouting(9, 0x05);
    CHECK((p.state().reg[23] >> 4) == 9, "resonance in high nibble of reg 0x17");
    CHECK((p.state().reg[23] & 0x0F) == 0x05, "full routing bits written together");
    p.setWaveform(0, sid::kPulse);
    CHECK((p.state().reg[4] & 0xF0) == sid::kPulse, "waveform bits set on control register");
    p.setSync(0, true);
    p.setRing(0, true);
    CHECK((p.state().reg[4] & sid::kSync) != 0 && (p.state().reg[4] & sid::kRing) != 0,
          "sync and ring bits set");
    CHECK((p.state().reg[4] & 0xF0) == sid::kPulse, "sync/ring keep the waveform bits");
    p.setSync(0, false);
    CHECK((p.state().reg[4] & sid::kSync) == 0 && (p.state().reg[4] & sid::kRing) != 0,
          "clearing sync leaves ring set");

    // Pitch modulation only applies while a note is sounding.
    AsidVoicePlayer pm;
    pm.setTargetVoice(0);
    CHECK(pm.setPitchMod(0, 1.0).empty(), "pitch mod does nothing with no note");
    pm.noteOn(0, 60, 100);
    auto freq0 = [&] { return pm.state().reg[0] | (pm.state().reg[1] << 8); };
    const int baseFreq = freq0();
    CHECK(!pm.setPitchMod(0, 12.0).empty(), "pitch mod writes frequency while a note sounds");
    CHECK(freq0() > baseFreq, "an octave up raises the frequency value");
    pm.noteOff(0, 60);
    CHECK(pm.setPitchMod(0, 5.0).empty(), "pitch mod stops once the note is released");

    // Coarse/fine tune offsets the played frequency.
    AsidVoicePlayer tune;
    tune.setTargetVoice(0);
    tune.noteOn(0, 60, 100);
    const int freqPlain = tune.state().reg[0] | (tune.state().reg[1] << 8);
    tune.setPitchOffset(12.0);   // +1 octave
    tune.noteOn(0, 60, 100);     // retrigger to pick up the offset
    const int freqUp = tune.state().reg[0] | (tune.state().reg[1] << 8);
    CHECK(freqUp > freqPlain * 3 / 2, "coarse tune up raises the played frequency (about double an octave)");
    CHECK(!tune.setPitchMod(0, 0.0).empty() && (tune.state().reg[0] | (tune.state().reg[1] << 8)) == freqUp,
          "the tune offset folds into pitch modulation too");
}

static void testLfo() {
    Lfo lfo;
    lfo.reset();

    // Sine landmarks.
    lfo.setShape(LfoShape::Sine);
    lfo.setPhase(0.0);
    CHECK(std::abs(lfo.value()) < 1e-9, "sine at phase 0 is 0");
    lfo.setPhase(0.25);
    CHECK(std::abs(lfo.value() - 1.0) < 1e-9, "sine at phase 0.25 is +1");
    lfo.setPhase(0.75);
    CHECK(std::abs(lfo.value() + 1.0) < 1e-9, "sine at phase 0.75 is -1");

    // Triangle: -1 at the ends, +1 at the middle.
    lfo.setShape(LfoShape::Triangle);
    lfo.setPhase(0.0);
    CHECK(std::abs(lfo.value() + 1.0) < 1e-9, "triangle at phase 0 is -1");
    lfo.setPhase(0.5);
    CHECK(std::abs(lfo.value() - 1.0) < 1e-9, "triangle at phase 0.5 is +1");

    // Saw up and square.
    lfo.setShape(LfoShape::SawUp);
    lfo.setPhase(0.0);
    CHECK(std::abs(lfo.value() + 1.0) < 1e-9, "saw up starts at -1");
    lfo.setShape(LfoShape::Square);
    lfo.setPhase(0.25);
    CHECK(lfo.value() > 0.0, "square is high in the first half");
    lfo.setPhase(0.75);
    CHECK(lfo.value() < 0.0, "square is low in the second half");

    // Free-running advance accumulates phase and wraps.
    lfo.setShape(LfoShape::Sine);
    lfo.setPhase(0.0);
    lfo.advance(0.5, 1.0);  // 1 Hz for half a second -> phase 0.5
    CHECK(std::abs(lfo.phase() - 0.5) < 1e-9, "advance moves phase by dt*rate");
    lfo.advance(0.75, 1.0);  // crosses 1.0 -> wraps to 0.25
    CHECK(std::abs(lfo.phase() - 0.25) < 1e-9, "advance wraps the phase into 0..1");

    // setPhase takes the song position in CYCLES, so 1.05 is "just into cycle 1".
    // Sample & Hold holds one value per cycle and takes a new one at each boundary.
    lfo.setShape(LfoShape::SampleHold);
    lfo.reset();
    lfo.setPhase(0.1);
    const double a = lfo.value();
    lfo.setPhase(0.4);
    CHECK(std::abs(lfo.value() - a) < 1e-12, "sample & hold holds its value within a cycle");
    lfo.setPhase(1.05);  // into the next cycle
    const double b = lfo.value();
    CHECK(std::abs(b - a) > 1e-12, "sample & hold picks a new value on a cycle boundary");
    CHECK(b >= -1.0 && b <= 1.0, "sample & hold stays bipolar");

    // A backward jump is the transport looping, not a new cycle: going back inside an
    // earlier cycle must not re-roll the value, which is what made a looped bar sound
    // different on every pass.
    lfo.setPhase(0.5);  // loop back into cycle 0
    CHECK(std::abs(lfo.value() - b) < 1e-12, "a backward jump does not re-roll sample & hold");
    lfo.setPhase(1.5);  // forward across the boundary again
    CHECK(std::abs(lfo.value() - b) > 1e-12, "crossing forward again does advance it");

    // Random glides within a cycle and stays continuous across a boundary.
    lfo.setShape(LfoShape::Random);
    lfo.reset();
    lfo.setPhase(0.0);
    const double g0 = lfo.value();
    lfo.setPhase(0.5);
    CHECK(std::abs(lfo.value() - g0) > 1e-9, "random glides within a cycle, it does not hold");
    lfo.setPhase(0.999);
    const double gEnd = lfo.value();
    lfo.setPhase(1.001);  // boundary: rndFrom becomes the previous rndTo
    CHECK(std::abs(gEnd - lfo.value()) < 0.05, "random is continuous across the boundary");
    CHECK(lfo.value() >= -1.0 && lfo.value() <= 1.0, "random glide stays bipolar");

    // A step covering several cycles must advance the endpoints once per cycle, not
    // once per call. Reachable when the modulation clock resumes after a gap (dt is
    // clamped to 4x its interval) at a high rate. Compare against stepping one cycle
    // at a time: same number of boundaries must give the same value.
    Lfo fast, slow;
    fast.setShape(LfoShape::SampleHold);
    slow.setShape(LfoShape::SampleHold);
    fast.reset();
    slow.reset();
    fast.advance(0.16, 20.0);  // 3.2 cycles in one step
    for (int i = 0; i < 3; ++i) slow.advance(0.05, 20.0);  // 1 cycle at a time, 3 times
    CHECK(std::abs(fast.phase() - 0.2) < 1e-9, "a multi-cycle step lands on the right phase");
    CHECK(std::abs(fast.value() - slow.value()) < 1e-12,
          "a multi-cycle step wraps once per cycle crossed");

    // Zero and negative movement leave the phase alone rather than wrapping.
    Lfo still;
    still.reset();
    still.advance(0.0, 20.0);
    CHECK(std::abs(still.phase()) < 1e-12, "advancing by nothing does not move the phase");
    still.advance(0.1, -5.0);
    CHECK(std::abs(still.phase()) < 1e-12, "a negative rate does not move the phase backwards");
}

static void testWaveTable() {
    WaveTablePlayer wt;

    // Speed 1: one step per frame, running 0,1,2 then looping to 0.
    wt.configure(3, 0, 1);
    CHECK(!wt.active(), "wavetable inactive before trigger");
    wt.trigger();
    CHECK(wt.active() && wt.currentStep() == 0, "trigger starts at step 0");
    wt.advanceFrame();
    CHECK(wt.currentStep() == 1, "advance moves to step 1");
    wt.advanceFrame();
    CHECK(wt.currentStep() == 2, "advance moves to step 2");
    wt.advanceFrame();
    CHECK(wt.currentStep() == 0, "wraps back to the loop point (0)");

    // Loop point in the middle: 0,1,2,3 then loops to 2,3,2,3...
    wt.configure(4, 2, 1);
    wt.trigger();
    for (int i = 0; i < 3; ++i) wt.advanceFrame();  // -> step 3
    CHECK(wt.currentStep() == 3, "reaches the last step");
    wt.advanceFrame();
    CHECK(wt.currentStep() == 2, "loops to the mid loop point, not 0");

    // Speed 3: a step holds for three frames.
    wt.configure(2, 0, 3);
    wt.trigger();
    wt.advanceFrame();
    wt.advanceFrame();
    CHECK(wt.currentStep() == 0, "speed 3 holds the step for 2 frames");
    wt.advanceFrame();
    CHECK(wt.currentStep() == 1, "speed 3 advances on the 3rd frame");

    // Loop clamped into range, and an empty table never activates.
    wt.configure(2, 9, 1);
    wt.trigger();
    wt.advanceFrame();
    wt.advanceFrame();
    CHECK(wt.currentStep() == 1, "out-of-range loop clamps to the last step");
    wt.configure(0, 0, 1);
    wt.trigger();
    CHECK(!wt.active() && wt.currentStep() == -1, "empty table stays inactive");

    // stop() halts playback.
    wt.configure(3, 0, 1);
    wt.trigger();
    wt.stop();
    CHECK(!wt.active(), "stop halts the wavetable");
}

static void testWaveformBits() {
    using namespace sid;
    // Bits OR together.
    CHECK(waveformBits(false, false, false, false) == 0, "no waveform selected is silent");
    CHECK(waveformBits(true, false, false, false) == kTriangle, "triangle only");
    CHECK(waveformBits(false, true, false, false) == kSaw, "saw only");
    CHECK(waveformBits(false, false, true, false) == kPulse, "pulse only");
    CHECK(waveformBits(true, true, false, false) == (kTriangle | kSaw), "triangle + saw combine");
    CHECK(waveformBits(true, true, true, false) == (kTriangle | kSaw | kPulse), "tri + saw + pulse combine");
    // Noise is exclusive: it wins alone, whatever else is set.
    CHECK(waveformBits(false, false, false, true) == kNoise, "noise only");
    CHECK(waveformBits(true, true, true, true) == kNoise, "noise locks out the other waveforms");
    CHECK(waveformBits(true, false, false, true) == kNoise, "noise beats triangle");
}

int main() {
    testDirectProgramFraming();
    testParamAddresses();
    testBipolarEncoding();
    testCcMap();
    testPatchRoundTrip();
    testRegistrySanity();
    testEnumChoices();
    testSysExAssembler();
    testSyxAndLibrary();
    testVoiceEngine();
    testAsid();
    testAsidPlayer();
    testLfo();
    testWaveTable();
    testWaveformBits();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
