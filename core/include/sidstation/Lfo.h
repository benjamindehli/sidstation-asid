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
    // Tempo sync: set the phase directly (wraps into 0..1). The caller derives it
    // from the song position and the note division.
    void setPhase(double phase01);

    double phase() const { return ph; }
    double value() const;  // bipolar [-1, 1] at the current phase and shape

    void reset();

private:
    void onWrap();  // pick the next random endpoint at a cycle boundary

    LfoShape shape = LfoShape::Sine;
    double ph = 0.0;
    unsigned int rng = 0x1234567u;  // deterministic, so the random shapes reproduce in tests
    // Two endpoints per cycle: Sample & Hold sits on rndFrom, Random glides from
    // rndFrom to rndTo across the cycle. On a wrap rndTo becomes the new rndFrom,
    // so the glide is continuous.
    double rndFrom = 0.0, rndTo = 0.0;
};

}  // namespace sidstation
