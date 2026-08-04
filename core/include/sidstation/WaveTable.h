#pragma once

namespace sidstation {

// Plays back the step POSITION of a per-voice wavetable, the software feature the
// SidStation and the C64 trackers use to sequence a voice's waveform (and an
// arpeggio) per frame. The 6581 has no such table; a host clocks this once per
// frame (~50 Hz on PAL) and writes the resulting waveform/frequency to the SID.
//
// The step CONTENTS (which waveform, what arpeggio offset) live in the host, as
// parameters. This class only owns the timing: advance one step every `speed`
// frames and jump back to `loopPoint` at the end.
class WaveTablePlayer {
public:
    // length: number of active steps (0 = table empty/off).
    // loopPoint: step to jump to after the last (clamped into range).
    // speed: frames per step (>= 1); 1 = a new step every frame.
    void configure(int length, int loopPoint, int speed);

    void trigger();        // note-on: restart at step 0
    void stop();           // note-off: go inactive
    void advanceFrame();   // call once per frame; advances the step per `speed`

    int currentStep() const { return active_ ? pos_ : -1; }  // -1 when inactive
    bool active() const { return active_; }

private:
    int length_ = 0, loop_ = 0, speed_ = 1;
    int pos_ = 0, frames_ = 0;
    bool active_ = false;
};

}  // namespace sidstation
