#include "sidstation/Lfo.h"

#include <cmath>

namespace sidstation {

namespace {
constexpr double kPi = 3.14159265358979323846;

double frac(double x) { return x - std::floor(x); }

// Small deterministic xorshift, so the Random shape is reproducible in tests.
double randBipolar(unsigned int& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (s / 4294967295.0) * 2.0 - 1.0;
}
}  // namespace

void Lfo::onWrap() {
    rndFrom = rndTo;            // continue from where the last cycle ended
    rndTo = randBipolar(rng);  // and head toward a fresh value
}

void Lfo::reset() {
    ph = 0.0;
    rng = 0x1234567u;
    rndFrom = randBipolar(rng);
    rndTo = randBipolar(rng);
}

void Lfo::advance(double dtSeconds, double rateHz) {
    const double before = ph;
    ph = frac(ph + dtSeconds * rateHz);
    if (ph < before) onWrap();  // passed 1.0
}

void Lfo::setPhase(double phase01) {
    const double before = ph;
    ph = frac(phase01);
    if (ph < before) onWrap();
}

double Lfo::value() const {
    switch (shape) {
        case LfoShape::Sine:     return std::sin(2.0 * kPi * ph);
        case LfoShape::Triangle: return 1.0 - 4.0 * std::abs(ph - 0.5);
        case LfoShape::SawUp:    return 2.0 * ph - 1.0;
        case LfoShape::SawDown:  return 1.0 - 2.0 * ph;
        case LfoShape::Square:     return ph < 0.5 ? 1.0 : -1.0;
        case LfoShape::SampleHold: return rndFrom;
        case LfoShape::Random:     return rndFrom + (rndTo - rndFrom) * ph;  // glide across the cycle
    }
    return 0.0;
}

}  // namespace sidstation
