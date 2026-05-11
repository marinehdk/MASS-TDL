#include "m5_tactical_planner/common/units.hpp"
// All conversions are constexpr/inline in the header.
// wrap_pi() and wrap_two_pi() use std::fmod — no additional linkage needed.
// This translation unit satisfies the linker.
namespace mass_l3::m5::units {}
