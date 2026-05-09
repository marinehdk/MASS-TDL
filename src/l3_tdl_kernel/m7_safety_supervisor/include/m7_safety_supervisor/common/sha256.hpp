#ifndef M7_SAFETY_SUPERVISOR_COMMON_SHA256_HPP_
#define M7_SAFETY_SUPERVISOR_COMMON_SHA256_HPP_

#include <array>
#include <cstdint>
#include <string_view>

namespace mass_l3::m7::common {

// Returns SHA-256 digest of input. Deterministic, no allocation.
// Algorithm: NIST FIPS 180-4.
[[nodiscard]] std::array<uint8_t, 32>
sha256(std::string_view input) noexcept;

}  // namespace mass_l3::m7::common

#endif  // M7_SAFETY_SUPERVISOR_COMMON_SHA256_HPP_
