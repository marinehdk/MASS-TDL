#include "m7_safety_supervisor/sotif/sliding_window_15s.hpp"

namespace mass_l3::m7::sotif {

void SlidingWindow15s::push(bool event) noexcept {
  if (size_ < kCapacity) {
    // Buffer not yet full — just append
    buffer_[head_] = event;
    ++head_;
    ++size_;
    if (event) { ++running_total_; }
  } else {
    // Buffer full — overwrite oldest (FIFO circular)
    if (head_ == kCapacity) { head_ = 0; }
    bool const kOldest = buffer_[head_];
    if (kOldest) { --running_total_; }
    buffer_[head_] = event;
    if (event) { ++running_total_; }
    ++head_;
  }
}

float SlidingWindow15s::rate() const noexcept {
  if (size_ == 0) { return 0.0F; }
  return static_cast<float>(running_total_) / static_cast<float>(size_);
}

std::uint16_t SlidingWindow15s::violation_count() const noexcept {
  return running_total_;
}

void SlidingWindow15s::reset() noexcept {
  head_ = 0;
  size_ = 0;
  running_total_ = 0;
}

}  // namespace mass_l3::m7::sotif
