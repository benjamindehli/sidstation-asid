#pragma once

namespace sidstation {

// Plays back the step POSITION of a per-voice wavetable, the software feature
// the SidStation and the C64 trackers use to sequence a voice's waveform (and
// an arpeggio) per frame. The 6581 has no such table; a host clocks this once
// per frame (~50 Hz on PAL) and writes the resulting waveform/frequency to the
// SID.
//
// The step CONTENTS (which waveform, what arpeggio offset) live in the host, as
// parameters. This class only owns the timing: advance one step every `speed`
// frames and jump back to `loopPoint` at the end.
//
// A step can also be given a length in time instead of in frames, which is what
// tempo sync uses (a note division converted to steps per second). The two are
// alternative clocks for the same position, so the caller picks one per frame
// and the step, length and loop behaviour is identical either way.
class WaveTablePlayer {
public:
  // length: number of active steps (0 = table empty/off).
  // loopPoint: step to jump to after the last (clamped into range).
  // speed: frames per step (>= 1); 1 = a new step every frame.
  void configure(int length, int loopPoint, int speed);

  void trigger();      // note-on: restart at step 0
  void stop();         // note-off: go inactive
  void advanceFrame(); // call once per frame; advances the step per `speed`
  // Tempo sync: advance by dtSeconds at stepsPerSecond, stepping each time the
  // accumulated fraction of a step crosses a boundary. The remainder is kept,
  // so step boundaries do not drift even though the caller's frames land on a
  // coarser grid than the step length. A non-positive rate holds the step.
  void advanceSeconds(double dtSeconds, double stepsPerSecond);

  int currentStep() const { return active_ ? pos_ : -1; } // -1 when inactive
  bool active() const { return active_; }

private:
  void step(); // advance one step, looping at the end

  // A single call can cover many steps if the step length is short next to the
  // caller's frame interval. Bounded so the loop stays finite: past this the
  // table has cycled several times over anyway.
  static constexpr int kMaxStepsPerCall = 64;

  int length_ = 0, loop_ = 0, speed_ = 1;
  int pos_ = 0, frames_ = 0;
  double acc_ = 0.0; // tempo sync only: fraction of the current step elapsed
  bool active_ = false;
};

} // namespace sidstation
