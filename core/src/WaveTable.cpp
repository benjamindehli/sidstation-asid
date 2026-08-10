#include "sidstation/WaveTable.h"

namespace sidstation {

void WaveTablePlayer::configure(int length, int loopPoint, int speed) {
  length_ = length < 0 ? 0 : length;
  speed_ = speed < 1 ? 1 : speed;
  // Loop must land on a real step; clamp so an empty table cannot loop.
  if (length_ == 0)
    loop_ = 0;
  else
    loop_ =
        loopPoint < 0 ? 0 : (loopPoint >= length_ ? length_ - 1 : loopPoint);
  if (pos_ >= length_)
    pos_ = loop_; // a shrunk table must not point past the end
  if (active_ && length_ == 0)
    active_ = false;
}

void WaveTablePlayer::trigger() {
  pos_ = 0;
  frames_ = 0;
  active_ = length_ > 0;
}

void WaveTablePlayer::stop() { active_ = false; }

void WaveTablePlayer::advanceFrame() {
  if (!active_)
    return;
  if (++frames_ >= speed_) {
    frames_ = 0;
    if (++pos_ >= length_)
      pos_ = loop_;
  }
}

} // namespace sidstation
