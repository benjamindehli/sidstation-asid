// Minimal dependency-free test harness for the SidStation core protocol lib.
// Cross-checks encoders against literal byte sequences documented in the
// SidStation Owners Manual (r22b, OS1.1), pages 39-43.
#include <cstdio>
#include <string>

#include "sidstation/ControllerMap.h"
#include "sidstation/DirectProgram.h"
#include "sidstation/Parameters.h"
#include "sidstation/Patch.h"

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
    // Manual example (page 43): "DP: 01 76 0f 04 08" — first table position = Noise.
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
        // LFOs (page 42) — these confirm the high/low split handling
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

    // Building a CC message from a param+value.
    const ParamInfo* cutoff = findParamById("filter.cutoff");
    checkBytes("CC message cutoff=64", controlChange(0, *cutoff, 64), Bytes{0xB0, 27, 64});
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

int main() {
    testDirectProgramFraming();
    testParamAddresses();
    testBipolarEncoding();
    testCcMap();
    testPatchRoundTrip();
    testRegistrySanity();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
