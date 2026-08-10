// Plugin-side low-frequency oscillator.
//
// The SID chip has no LFOs of its own, so movement like pulse-width modulation
// has to be generated here and streamed to a register over ASID. This is that
// generator: a bipolar waveform in [-1, 1] that the caller advances, either
// free-running at a rate in Hz or phase-locked to the host timeline for tempo
// sync. Framework agnostic and unit tested. Turning the output into an actual
// register value (and the streaming rate) is the caller's job.
#pragma once

namespace sidstation {

// SampleHold steps to a new random value each cycle; Random picks the same
// values but glides between them. Keep this order in sync with the UI choices.
enum class LfoShape { Sine, Triangle, SawUp, SawDown, Square, SampleHold, Random };

class Lfo {
public:
    void setShape(LfoShape s) { shape = s; }
    LfoShape getShape() const { return shape; }

    // Free-running: move the phase forward by dtSeconds at rateHz.
    void advance(double dtSeconds, double rateHz);
    // Tempo sync: set the position from the song, in CYCLES (song position divided by
    // the note division), so 4.25 means "a quarter into cycle 4". The fractional part
    // becomes the phase; the integer part is kept as the cycle index, which is what
    // lets a cycle boundary be counted rather than guessed. Crossing boundaries
    // forward advances the random endpoints; jumping backward (the transport looping)
    // does not.
    void setPhase(double cyclePosition);

    double phase() const { return ph; }
    double value() const;  // bipolar [-1, 1] at the current phase and shape

    void reset();

private:
    void onWrap();               // pick the next random endpoint at a cycle boundary
    void applyWraps(double n);   // onWrap once per boundary crossed, bounded

    // A single step can cover many cycles (a resumed modulation clock at a high rate,
    // or a transport jump), so the wrap count is bounded: past this the random
    // endpoints are indistinguishable anyway, and it keeps the loop finite.
    static constexpr int kMaxWrapsPerStep = 64;

    LfoShape shape = LfoShape::Sine;
    double ph = 0.0;
    // Tempo sync only: the last cycle index seen, so boundaries can be counted from
    // the song position instead of inferred from the phase going backwards.
    double cycle = 0.0;
    bool cycleLocked = false;    // false until the first setPhase establishes a cycle
    unsigned int rng = 0x1234567u;  // deterministic, so the random shapes reproduce in tests
    // Two endpoints per cycle: Sample & Hold sits on rndFrom, Random glides from
    // rndFrom to rndTo across the cycle. On a wrap rndTo becomes the new rndFrom,
    // so the glide is continuous.
    double rndFrom = 0.0, rndTo = 0.0;
};

}  // namespace sidstation
