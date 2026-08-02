// Reassembles complete SysEx messages from a raw incoming MIDI byte stream.
//
// MIDI input arrives in arbitrary chunks: a single SysEx (e.g. a patch dump)
// may span many callbacks, and system-realtime bytes (0xF8..0xFF) can be
// interleaved inside it. Feed bytes in as they arrive; get back complete
// F0..F7 messages as they finish. Framework-agnostic and header-only so the
// plugin's MIDI input and the probe can share it.
#pragma once

#include <cstddef>

#include "SysEx.h"

namespace sidstation {

class SysExAssembler {
public:
    // Feeds raw MIDI bytes; returns every complete SysEx message that finished
    // within this call (usually zero or one, but more for a bulk "all patches"
    // stream).
    std::vector<Bytes> feed(const Byte* data, std::size_t len) {
        std::vector<Bytes> out;
        for (std::size_t i = 0; i < len; ++i) {
            const Byte b = data[i];
            if (b == sysex::kStart) {          // F0: (re)start, discard any partial
                current_.assign(1, b);
                inSysex_ = true;
            } else if (b >= 0xF8) {            // system realtime: ignore for assembly
                continue;
            } else if (inSysex_) {
                current_.push_back(b);
                if (b == sysex::kEnd) {        // F7: complete
                    out.push_back(current_);
                    current_.clear();
                    inSysex_ = false;
                } else if (b & 0x80) {         // stray status byte aborts the SysEx
                    current_.clear();
                    inSysex_ = false;
                }
            }
            // bytes outside a SysEx are ignored (channel-voice traffic etc.)
        }
        return out;
    }

    std::vector<Bytes> feed(const Bytes& data) {
        return feed(data.data(), data.size());
    }

    void reset() {
        current_.clear();
        inSysex_ = false;
    }

    bool inProgress() const { return inSysex_; }

private:
    Bytes current_;
    bool  inSysex_ = false;
};

}  // namespace sidstation
