#include "sidstation/Patch.h"

#include <algorithm>

namespace sidstation {
namespace {
constexpr Byte kPad          = 0x2D;
constexpr int  kPadCount     = 24;
constexpr Byte kDataMagic    = 0x45;  // 'E' - start of patch data
constexpr Byte kAllClearId   = 0x01;
constexpr Byte kAllClearMagic = 0x45;
constexpr int  kAllClearPad  = 14;
}  // namespace

std::string Patch::name() const {
    std::string n;
    for (std::size_t i = 0; i < kNameLength && i < data.size(); ++i) {
        char c = static_cast<char>(data[i]);
        if (c == '\0') break;
        n.push_back(c);
    }
    // Trim trailing spaces for a tidy display name.
    while (!n.empty() && n.back() == ' ') n.pop_back();
    return n;
}

void Patch::setName(const std::string& n) {
    if (data.size() < kNameLength) data.resize(kNameLength, 0x20);
    for (std::size_t i = 0; i < kNameLength; ++i)
        data[i] = i < n.size() ? static_cast<Byte>(n[i]) : 0x20;  // pad with spaces
}

Bytes encodePatchDump(const Patch& patch) {
    Bytes out;
    sysex::appendInit(out);
    out.push_back(static_cast<Byte>(sysex::MessageType::PatchDump));
    out.push_back(0x00);  // dump version

    const std::size_t size = patch.data.size();
    out.push_back(static_cast<Byte>((size >> 7) & 0x03));  // %000000aa
    out.push_back(static_cast<Byte>(size & 0x7F));         // %0aaaaaaa

    for (int i = 0; i < kPadCount; ++i) out.push_back(kPad);
    out.push_back(kDataMagic);

    // Name region: first 10 bytes verbatim (ASCII).
    const std::size_t nameLen = std::min(patch.data.size(), Patch::kNameLength);
    for (std::size_t i = 0; i < nameLen; ++i) out.push_back(patch.data[i]);

    // Remaining bytes: low nibble first, then high nibble (the order the unit
    // uses, confirmed against a real OS 1.11 dump).
    for (std::size_t i = Patch::kNameLength; i < patch.data.size(); ++i) {
        out.push_back(static_cast<Byte>(patch.data[i] & 0x0F));
        out.push_back(static_cast<Byte>((patch.data[i] >> 4) & 0x0F));
    }

    out.push_back(sysex::kEnd);
    return out;
}

std::optional<Patch> decodePatchDump(const Bytes& msg) {
    if (!sysex::hasValidInit(msg)) return std::nullopt;
    if (msg.size() < 12) return std::nullopt;
    if (msg[6] != static_cast<Byte>(sysex::MessageType::PatchDump)) return std::nullopt;
    if (msg.back() != sysex::kEnd) return std::nullopt;

    // Locate the data magic after the 24-byte pad (index 7=version, 8/9=size,
    // 10..33=pad, 34=magic).
    constexpr std::size_t kMagicIndex = 34;
    if (msg.size() <= kMagicIndex + 1 || msg[kMagicIndex] != kDataMagic)
        return std::nullopt;

    const std::size_t begin = kMagicIndex + 1;
    const std::size_t end   = msg.size() - 1;  // exclusive of F7
    if (end < begin) return std::nullopt;

    Patch patch;
    std::size_t i = begin;
    // Name: 10 ASCII bytes.
    for (std::size_t k = 0; k < Patch::kNameLength && i < end; ++k, ++i)
        patch.data.push_back(msg[i]);
    // Remainder: nibble pairs -> bytes. The unit sends the low nibble first,
    // then the high nibble (confirmed against a real OS 1.11 dump).
    for (; i + 1 <= end; i += 2) {
        Byte lo = msg[i] & 0x0F;
        Byte hi = msg[i + 1] & 0x0F;
        patch.data.push_back(static_cast<Byte>((hi << 4) | lo));
    }
    return patch;
}

Bytes encodePatchAllClear() {
    Bytes out;
    sysex::appendInit(out);
    out.push_back(kAllClearId);
    out.push_back(kAllClearMagic);
    for (int i = 0; i < kAllClearPad; ++i) out.push_back(kPad);
    out.push_back(sysex::kEnd);
    return out;
}

Bytes encodeSkipPatch() {
    Bytes out;
    sysex::appendInit(out);
    out.push_back(static_cast<Byte>(sysex::MessageType::SkipPatch));
    out.push_back(sysex::kEnd);
    return out;
}

}  // namespace sidstation
