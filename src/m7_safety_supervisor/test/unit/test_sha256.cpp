#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>

#include "m7_safety_supervisor/common/sha256.hpp"

using mass_l3::m7::common::sha256;

namespace {

// ---------------------------------------------------------------------------
// Helper: convert 64-char hex string to 32-byte array
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::array<uint8_t, 32> hex_to_bytes(const char* hex) noexcept
{
    std::array<uint8_t, 32> bytes{};
    for (std::size_t i = 0U; i < 32U; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        char const kCh = hex[i * 2U];
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        char const kCl = hex[(i * 2U) + 1U];
        uint8_t hi = (kCh >= '0' && kCh <= '9') ? static_cast<uint8_t>(kCh - '0')
                   : static_cast<uint8_t>(kCh - 'a' + 10);
        uint8_t lo = (kCl >= '0' && kCl <= '9') ? static_cast<uint8_t>(kCl - '0')
                   : static_cast<uint8_t>(kCl - 'a' + 10);
        bytes[i] = static_cast<uint8_t>((static_cast<uint8_t>(hi << 4U)) | lo);
    }
    return bytes;
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: NIST FIPS 180-4 known vector — empty string
// Expected: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
// ---------------------------------------------------------------------------
TEST(Sha256Test, Sha256_EmptyString)
{
    auto const kResult   = sha256("");
    auto const kExpected = hex_to_bytes(
        "e3b0c44298fc1c149afbf4c8996fb924"
        "27ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(kResult, kExpected);
}

// ---------------------------------------------------------------------------
// Test 2: NIST FIPS 180-4 known vector — "abc"
// Expected: ba7816bf8f01cfea414140de5dae2ec73b00361bbef0469348423f656b7f4fbe
// ---------------------------------------------------------------------------
TEST(Sha256Test, Sha256_Abc)
{
    auto const kResult   = sha256("abc");
    auto const kExpected = hex_to_bytes(
        "ba7816bf8f01cfea414140de5dae2ec7"
        "3b00361bbef0469348423f656b7f4fbe");
    EXPECT_EQ(kResult, kExpected);
}

// ---------------------------------------------------------------------------
// Test 3: output is always 32 bytes
// ---------------------------------------------------------------------------
TEST(Sha256Test, Sha256_OutputIs32Bytes)
{
    auto const kResult = sha256("any input");
    EXPECT_EQ(kResult.size(), 32U);
}

// ---------------------------------------------------------------------------
// Test 4: deterministic — same input produces same digest
// ---------------------------------------------------------------------------
TEST(Sha256Test, Sha256_Consistency)
{
    std::string const kInput = "consistent input string";
    auto const kFirst  = sha256(kInput);
    auto const kSecond = sha256(kInput);
    EXPECT_EQ(kFirst, kSecond);
}

// ---------------------------------------------------------------------------
// Test 5: distinct inputs produce distinct digests
// ---------------------------------------------------------------------------
TEST(Sha256Test, Sha256_DifferentInputsDifferentOutput)
{
    auto const kD1 = sha256("input_one");
    auto const kD2 = sha256("input_two");
    EXPECT_NE(kD1, kD2);
}

// ---------------------------------------------------------------------------
// Test 6: NIST FIPS 180-4 Appendix B.2 — 56-byte input, two-block padding path
// Expected: 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
// ---------------------------------------------------------------------------
TEST(Sha256Test, Sha256_TwoBlockPadding_NistAppendixB2)
{
    auto const kResult = sha256(
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    auto const kExpected = hex_to_bytes(
        "248d6a61d20638b8e5c026930c3e6039"
        "a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(kResult, kExpected);
}
