#ifndef MASS_L3_M5_COMMON_SHA256_HPP_
#define MASS_L3_M5_COMMON_SHA256_HPP_

#include <array>
#include <cstdint>
#include <string_view>

namespace mass_l3::m5::common {

// Returns SHA-256 digest of input. Deterministic, no allocation.
// Algorithm: NIST FIPS 180-4. Used to sign ASDRRecord.decision_json.
[[nodiscard]] std::array<uint8_t, 32>
sha256(std::string_view input) noexcept;

}  // namespace mass_l3::m5::common

#endif  // MASS_L3_M5_COMMON_SHA256_HPP_
