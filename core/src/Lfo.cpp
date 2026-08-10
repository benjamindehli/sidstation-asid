#include "sidstation/Lfo.h"

#include <cmath>

namespace sidstation {

namespace {
constexpr double kPi = 3.14159265358979323846;

// Small deterministic xorshift, so the Random shape is reproducible in tests.
double randBipolar(unsigned int &s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return (s / 4294967295.0) * 2.0 - 1.0;
}
} // namespace

void Lfo::onWrap() {
  rndFrom = rndTo;          // continue from where the last cycle ended
  rndTo = randBipolar(rng); // and head toward a fresh value
}

void Lfo::reset() {
  ph = 0.0;
  cycle = 0.0;
  cycleLocked = false;
  rng = 0x1234567u;
  rndFrom = randBipolar(rng);
  rndTo = randBipolar(rng);
}

// One onWrap per boundary crossed. Bounded, and NaN-safe: the comparison
// rejects it before the cast, which would otherwise be undefined.
void Lfo::applyWraps(double n) {
  if (!(n > 0.0))
    return;
  const int count = n >= static_cast<double>(kMaxWrapsPerStep)
                        ? kMaxWrapsPerStep
                        : static_cast<int>(n);
  for (int i = 0; i < count; ++i)
    onWrap();
}

void Lfo::advance(double dtSeconds, double rateHz) {
  const double delta = dtSeconds * rateHz;
  if (!(delta > 0.0))
    return; // nothing to do, and this rejects NaN too
  // ph is always in [0, 1), so the integer part of the sum IS the number of
  // cycle boundaries this step crossed. The old test was "did the phase end up
  // lower than it started", which sees at most one boundary and misses them
  // altogether when a step covers a whole cycle and lands above where it began.
  // That is reachable: the modulation clock clamps a resumed dt to 4x its
  // interval (160 ms at Eco) and the rate goes to 20 Hz, so a single step can
  // span 3.2 cycles.
  const double advanced = ph + delta;
  const double whole = std::floor(advanced);
  ph = advanced - whole;
  applyWraps(whole);
}

void Lfo::setPhase(double cyclePosition) {
  const double whole = std::floor(cyclePosition);
  ph = cyclePosition - whole;
  // First lock: adopt the cycle without treating it as a boundary crossing.
  if (!cycleLocked) {
    cycle = whole;
    cycleLocked = true;
    return;
  }
  const double crossed = whole - cycle;
  cycle = whole;
  // Forward only. A backward jump is the transport looping, and the old
  // phase-went-backwards test read that as a fresh cycle, so Sample & Hold
  // re-rolled its value on every pass of the loop instead of repeating it.
  applyWraps(crossed);
}

double Lfo::value() const {
  switch (shape) {
  case LfoShape::Sine:
    return std::sin(2.0 * kPi * ph);
  case LfoShape::Triangle:
    return 1.0 - 4.0 * std::abs(ph - 0.5);
  case LfoShape::SawUp:
    return 2.0 * ph - 1.0;
  case LfoShape::SawDown:
    return 1.0 - 2.0 * ph;
  case LfoShape::Square:
    return ph < 0.5 ? 1.0 : -1.0;
  case LfoShape::SampleHold:
    return rndFrom;
  case LfoShape::Random:
    return rndFrom + (rndTo - rndFrom) * ph; // glide across the cycle
  }
  return 0.0;
}

} // namespace sidstation
