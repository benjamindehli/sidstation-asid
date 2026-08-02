// sidprobe - a small standalone tool to talk to a real SidStation and validate
// the core protocol library against hardware (milestone 2).
//
// It sends Direct-Program / CC / note / raw messages and pretty-prints any
// incoming SysEx (decoding patch dumps and DP echoes), so we can confirm:
//   * that DP encoding actually moves the right knob on the unit,
//   * the real patch-dump framing (size field, name region), and
//   * whether setting an oscillator's fixed note retriggers the gate
//     (the behaviour the three-voice play engine depends on).
//
// Two backends, selected at compile time:
//   * __APPLE__  -> real CoreMIDI I/O.
//   * otherwise  -> a "dry-run" backend that prints the bytes it would send,
//                   so the command layer and encoders can be built and tested
//                   on any platform.
#include <cctype>
#include <cstdint>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "sidstation/ControllerMap.h"
#include "sidstation/DirectProgram.h"
#include "sidstation/Parameters.h"
#include "sidstation/Patch.h"

using namespace sidstation;

// ---------------------------------------------------------------------------
// Received-SysEx capture buffer. Incoming messages arrive on the MIDI thread,
// the REPL saves them from the main thread, so guard with a mutex.
// ---------------------------------------------------------------------------
static std::mutex g_rxMutex;
static Bytes g_rxAll;      // every received SysEx byte, in order, ready to save
static int   g_rxCount;    // number of complete SysEx messages captured

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------
static std::string toHex(const Bytes& b) {
    std::string s;
    char buf[4];
    for (Byte x : b) { std::snprintf(buf, sizeof buf, "%02X ", x); s += buf; }
    if (!s.empty()) s.pop_back();
    return s;
}

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Decodes and prints an incoming SysEx message (called from the MIDI thread on
// macOS). Kept dependency-light and self-contained.
[[maybe_unused]] static void printIncoming(const Bytes& msg) {
    {
        std::lock_guard<std::mutex> lock(g_rxMutex);
        g_rxAll.insert(g_rxAll.end(), msg.begin(), msg.end());
        ++g_rxCount;
    }
    std::printf("\n<-- RX %zu bytes: %s\n", msg.size(), toHex(msg).c_str());

    if (auto patch = decodePatchDump(msg)) {
        std::printf("    Patch dump: name=\"%s\", %zu data bytes\n",
                    patch->name().c_str(), patch->data.size());
        // Round-trip check: re-encode and compare framing length.
        Bytes re = encodePatchDump(*patch);
        std::printf("    Round-trip re-encode: %zu bytes %s\n", re.size(),
                    re == msg ? "(identical)" : "(differs - inspect framing!)");
        return;
    }
    if (auto dp = decodeDirectProgram(msg); dp.valid) {
        std::printf("    Direct Program: pos=0x%X mask=0x%02X shift=%u data=0x%02X\n",
                    dp.address.position, dp.address.mask, dp.address.shift, dp.data);
        return;
    }
    if (sysex::hasValidInit(msg))
        std::printf("    (SidStation SysEx, type byte 0x%02X - not yet decoded)\n",
                    msg.size() > 6 ? msg[6] : 0);
    else
        std::printf("    (non-SidStation / unrecognised SysEx)\n");
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// MIDI backend - platform specific
// ---------------------------------------------------------------------------
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>

static std::string endpointName(MIDIEndpointRef ep) {
    CFStringRef cf = nullptr;
    if (MIDIObjectGetStringProperty(ep, kMIDIPropertyDisplayName, &cf) != noErr || !cf)
        return "<unknown>";
    char buf[256] = {0};
    CFStringGetCString(cf, buf, sizeof buf, kCFStringEncodingUTF8);
    CFRelease(cf);
    return buf;
}

static void readProc(const MIDIPacketList* pktList, void*, void*) {
    static Bytes accum;
    static bool inSysex = false;
    const MIDIPacket* packet = &pktList->packet[0];
    for (UInt32 i = 0; i < pktList->numPackets; ++i) {
        for (UInt16 j = 0; j < packet->length; ++j) {
            Byte b = packet->data[j];
            if (b == 0xF0) { accum.clear(); accum.push_back(b); inSysex = true; }
            else if (inSysex) {
                accum.push_back(b);
                if (b == 0xF7) { inSysex = false; printIncoming(accum); }
            }
        }
        packet = MIDIPacketNext(packet);
    }
}

struct MidiBackend {
    MIDIClientRef   client = 0;
    MIDIPortRef     outPort = 0, inPort = 0;
    MIDIEndpointRef dest = 0, source = 0;
    bool dryRun = false;

    static void listPorts() {
        std::printf("Destinations (send TO these):\n");
        for (ItemCount i = 0; i < MIDIGetNumberOfDestinations(); ++i)
            std::printf("  [%lu] %s\n", i, endpointName(MIDIGetDestination(i)).c_str());
        std::printf("Sources (receive FROM these):\n");
        for (ItemCount i = 0; i < MIDIGetNumberOfSources(); ++i)
            std::printf("  [%lu] %s\n", i, endpointName(MIDIGetSource(i)).c_str());
    }

    static int autoPick(bool dest) {
        ItemCount n = dest ? MIDIGetNumberOfDestinations() : MIDIGetNumberOfSources();
        for (ItemCount i = 0; i < n; ++i) {
            auto ep = dest ? MIDIGetDestination(i) : MIDIGetSource(i);
            if (lower(endpointName(ep)).find("sid") != std::string::npos) return (int)i;
        }
        return n > 0 ? 0 : -1;
    }

    bool open(int outIdx, int inIdx) {
        MIDIClientCreate(CFSTR("sidprobe"), nullptr, nullptr, &client);
        MIDIOutputPortCreate(client, CFSTR("sidprobe out"), &outPort);
        MIDIInputPortCreate(client, CFSTR("sidprobe in"), readProc, nullptr, &inPort);

        if (outIdx < 0) outIdx = autoPick(true);
        if (inIdx < 0) inIdx = autoPick(false);
        if (outIdx < 0 || (ItemCount)outIdx >= MIDIGetNumberOfDestinations()) {
            std::fprintf(stderr, "No MIDI destination available.\n");
            return false;
        }
        dest = MIDIGetDestination(outIdx);
        std::printf("Sending to:   [%d] %s\n", outIdx, endpointName(dest).c_str());
        if (inIdx >= 0 && (ItemCount)inIdx < MIDIGetNumberOfSources()) {
            source = MIDIGetSource(inIdx);
            MIDIPortConnectSource(inPort, source, nullptr);
            std::printf("Listening on: [%d] %s\n", inIdx, endpointName(source).c_str());
        } else {
            std::printf("Listening on: (none - RX disabled)\n");
        }
        return true;
    }

    void send(const Bytes& bytes) {
        Byte buffer[65536];
        MIDIPacketList* list = reinterpret_cast<MIDIPacketList*>(buffer);
        MIDIPacket* pkt = MIDIPacketListInit(list);
        pkt = MIDIPacketListAdd(list, sizeof buffer, pkt, 0, bytes.size(), bytes.data());
        if (!pkt) { std::fprintf(stderr, "message too large for packet list\n"); return; }
        OSStatus st = MIDISend(outPort, dest, list);
        if (st != noErr) std::fprintf(stderr, "MIDISend error %d\n", (int)st);
    }
};

#else  // ---- dry-run backend (non-Apple) ----------------------------------
struct MidiBackend {
    bool dryRun = true;
    static void listPorts() {
        std::printf("(dry-run build: no real MIDI ports - CoreMIDI is macOS-only)\n");
    }
    static int autoPick(bool) { return -1; }
    bool open(int, int) {
        std::printf("[dry-run] No hardware I/O. Messages are printed, not sent.\n");
        return true;
    }
    void send(const Bytes& bytes) {
        std::printf("[dry-run] TX %zu bytes: %s\n", bytes.size(), toHex(bytes).c_str());
    }
};
#endif

// ---------------------------------------------------------------------------
// Command layer (shared across backends)
// ---------------------------------------------------------------------------
static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream is(line);
    std::string tok;
    while (is >> tok) out.push_back(tok);
    return out;
}

static bool parseInt(const std::string& s, int& out) {
    try { size_t pos; out = std::stoi(s, &pos, 0); return pos == s.size(); }
    catch (...) { return false; }
}

static void listParams(const std::string& filter) {
    int count = 0;
    for (const auto& p : parameters()) {
        if (!filter.empty() && lower(p.id).find(lower(filter)) == std::string::npos) continue;
        std::printf("  %-24s %-20s range %d..%d%s\n", p.id.c_str(), p.group.c_str(),
                    p.minValue, p.maxValue, p.cc >= 0 ? (" cc=" + std::to_string(p.cc)).c_str() : "");
        ++count;
    }
    std::printf("(%d parameters)\n", count);
}

static void printHelp() {
    std::printf(
        "Commands:\n"
        "  list                       list MIDI ports\n"
        "  params [filter]            list parameter ids (optional substring filter)\n"
        "  send <id> <value>          send a Direct-Program edit (primary path)\n"
        "  cc   <id> <value>          send the parameter's MIDI CC instead\n"
        "  note <note> [vel] [ch]     note on  (defaults vel=100 ch=0)\n"
        "  off  <note> [ch]           note off\n"
        "  raw  <hex> [hex...]        send raw bytes, e.g. raw F0 00 20 3C 01 00 03 F7\n"
        "  skip                       send Skip-Patch (advance patch position)\n"
        "  rxinfo                     show how much SysEx has been captured\n"
        "  save <path.syx>            write all captured SysEx to a .syx file\n"
        "  clearrx                    clear the capture buffer\n"
        "  help                       this help\n"
        "  quit                       exit\n"
        "\nTip: dump a patch FROM the unit using its front panel, incoming SysEx is\n"
        "decoded and captured automatically, then `save before.syx`. (Patch all-clear\n"
        "is intentionally omitted, it wipes patch memory, use `raw` if you really\n"
        "need it.)\n");
}

// Executes one command. Returns false to quit.
static bool runCommand(MidiBackend& midi, const std::vector<std::string>& tk) {
    if (tk.empty()) return true;
    const std::string& cmd = tk[0];

    if (cmd == "quit" || cmd == "exit") return false;
    if (cmd == "help") { printHelp(); return true; }
    if (cmd == "list") { MidiBackend::listPorts(); return true; }
    if (cmd == "params") { listParams(tk.size() > 1 ? tk[1] : ""); return true; }

    if (cmd == "send" || cmd == "cc") {
        if (tk.size() < 3) { std::printf("usage: %s <id> <value>\n", cmd.c_str()); return true; }
        const ParamInfo* p = findParamById(tk[1]);
        if (!p) { std::printf("Unknown parameter '%s' (try: params %s)\n", tk[1].c_str(), tk[1].c_str()); return true; }
        int v;
        if (!parseInt(tk[2], v)) { std::printf("Bad value '%s'\n", tk[2].c_str()); return true; }
        Bytes msg;
        if (cmd == "send") {
            msg = directProgramFor(*p, v);
            std::printf("send %s = %d  ->  %s\n", p->id.c_str(), v, toHex(msg).c_str());
        } else {
            if (p->cc < 0) { std::printf("'%s' has no MIDI CC; use `send`.\n", p->id.c_str()); return true; }
            msg = controlChange(0, *p, v);
            std::printf("cc   %s = %d  ->  %s\n", p->id.c_str(), v, toHex(msg).c_str());
        }
        midi.send(msg);
        return true;
    }

    if (cmd == "note" || cmd == "off") {
        if (tk.size() < 2) { std::printf("usage: %s <note> ...\n", cmd.c_str()); return true; }
        int note, vel = 100, ch = 0;
        if (!parseInt(tk[1], note)) { std::printf("Bad note\n"); return true; }
        if (cmd == "note") {
            if (tk.size() > 2) parseInt(tk[2], vel);
            if (tk.size() > 3) parseInt(tk[3], ch);
            Bytes msg{static_cast<Byte>(0x90 | (ch & 0x0F)), static_cast<Byte>(note & 0x7F),
                      static_cast<Byte>(vel & 0x7F)};
            std::printf("note on  n=%d v=%d ch=%d  ->  %s\n", note, vel, ch, toHex(msg).c_str());
            midi.send(msg);
        } else {
            if (tk.size() > 2) parseInt(tk[2], ch);
            Bytes msg{static_cast<Byte>(0x80 | (ch & 0x0F)), static_cast<Byte>(note & 0x7F), 0};
            std::printf("note off n=%d ch=%d  ->  %s\n", note, ch, toHex(msg).c_str());
            midi.send(msg);
        }
        return true;
    }

    if (cmd == "raw") {
        Bytes msg;
        for (size_t i = 1; i < tk.size(); ++i) {
            int b;
            if (!parseInt("0x" + tk[i], b) && !parseInt(tk[i], b)) {
                std::printf("Bad hex byte '%s'\n", tk[i].c_str());
                return true;
            }
            msg.push_back(static_cast<Byte>(b & 0xFF));
        }
        if (msg.empty()) { std::printf("usage: raw <hex> [hex...]\n"); return true; }
        std::printf("raw  ->  %s\n", toHex(msg).c_str());
        midi.send(msg);
        return true;
    }

    if (cmd == "skip") {
        Bytes msg = encodeSkipPatch();
        std::printf("skip ->  %s\n", toHex(msg).c_str());
        midi.send(msg);
        return true;
    }

    if (cmd == "rxinfo") {
        std::lock_guard<std::mutex> lock(g_rxMutex);
        std::printf("captured %d SysEx message(s), %zu bytes\n", g_rxCount, g_rxAll.size());
        return true;
    }

    if (cmd == "clearrx") {
        std::lock_guard<std::mutex> lock(g_rxMutex);
        g_rxAll.clear();
        g_rxCount = 0;
        std::printf("capture buffer cleared\n");
        return true;
    }

    if (cmd == "save") {
        if (tk.size() < 2) { std::printf("usage: save <path.syx>\n"); return true; }
        std::lock_guard<std::mutex> lock(g_rxMutex);
        if (g_rxAll.empty()) { std::printf("nothing captured yet\n"); return true; }
        std::ofstream os(tk[1], std::ios::binary);
        if (!os) { std::printf("cannot open '%s' for writing\n", tk[1].c_str()); return true; }
        os.write(reinterpret_cast<const char*>(g_rxAll.data()),
                 static_cast<std::streamsize>(g_rxAll.size()));
        std::printf("saved %d message(s), %zu bytes to %s\n", g_rxCount, g_rxAll.size(),
                    tk[1].c_str());
        return true;
    }

    std::printf("Unknown command '%s' (try: help)\n", cmd.c_str());
    return true;
}

int main(int argc, char** argv) {
    int outIdx = -1, inIdx = -1;
    std::vector<std::string> oneShot;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) parseInt(argv[++i], outIdx);
        else if (a == "--in" && i + 1 < argc) parseInt(argv[++i], inIdx);
        else oneShot.push_back(a);
    }

    // `sidprobe list` should work without opening ports.
    if (oneShot.size() == 1 && oneShot[0] == "list") { MidiBackend::listPorts(); return 0; }

    MidiBackend midi;
    if (!midi.open(outIdx, inIdx)) return 1;

    if (!oneShot.empty()) {
        runCommand(midi, oneShot);
        return 0;
    }

    std::printf("\nsidprobe ready. Type `help`, or `quit` to exit.\n");
    std::string line;
    while (true) {
        std::printf("sid> ");
        std::fflush(stdout);
        if (!std::getline(std::cin, line)) break;
        if (!runCommand(midi, tokenize(line))) break;
    }
    return 0;
}
