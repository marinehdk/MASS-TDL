#ifndef MASS_L3_M8_SHA256_HPP_
#define MASS_L3_M8_SHA256_HPP_

#include <array>
#include <cstdint>
#include <string_view>

namespace mass_l3::m8::detail {

// Returns SHA-256 digest of input. Deterministic, no allocation.
// Algorithm: NIST FIPS 180-4.
[[nodiscard]] std::array<uint8_t, 32>
sha256(std::string_view input) noexcept;

}  // namespace mass_l3::m8::detail

#endif  // MASS_L3_M8_SHA256_HPP_
