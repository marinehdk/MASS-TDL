#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>

#include "m7_safety_supervisor/common/sha256.hpp"

using mass_l3::m7::common::sha256;

// ---------------------------------------------------------------------------
// Helper: convert 64-char hex string to 32-byte array
// ---------------------------------------------------------------------------
static std::array<uint8_t, 32> hex_to_bytes(const char* hex) noexcept
{
    std::array<uint8_t, 32> result{};
    for (std::size_t i = 0U; i < 32U; ++i) {
        uint8_t hi = 0U;
        uint8_t lo = 0U;
        char const ch  = hex[i * 2U];
        char const cl  = hex[i * 2U + 1U];
        hi = (ch >= '0' && ch <= '9') ? static_cast<uint8_t>(ch - '0')
           : static_cast<uint8_t>(ch - 'a' + 10);
        lo = (cl >= '0' && cl <= '9') ? static_cast<uint8_t>(cl - '0')
           : static_cast<uint8_t>(cl - 'a' + 10);
        result[i] = static_cast<uint8_t>((hi << 4U) | lo);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Test 1: NIST FIPS 180-4 known vector — empty string
// Expected: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
// ---------------------------------------------------------------------------
TEST(Sha256Test, Sha256_EmptyString)
{
    auto const result   = sha256("");
    auto const expected = hex_to_bytes(
        "e3b0c44298fc1c149afbf4c8996fb924"
        "27ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(result, expected);
}

// ---------------------------------------------------------------------------
// Test 2: NIST FIPS 180-4 known vector — "abc"
// Expected: ba7816bf8f01cfea414140de5dae2ec73b00361bbef0469348423f656b7f4fbe
// ---------------------------------------------------------------------------
TEST(Sha256Test, Sha256_Abc)
{
    auto const result   = sha256("abc");
    auto const expected = hex_to_bytes(
        "ba7816bf8f01cfea414140de5dae2ec7"
        "3b00361bbef0469348423f656b7f4fbe");
    EXPECT_EQ(result, expected);
}

// ---------------------------------------------------------------------------
// Test 3: output is always 32 bytes
// ---------------------------------------------------------------------------
TEST(Sha256Test, Sha256_OutputIs32Bytes)
{
    auto const result = sha256("any input");
    EXPECT_EQ(result.size(), 32U);
}

// ---------------------------------------------------------------------------
// Test 4: deterministic — same input produces same digest
// ---------------------------------------------------------------------------
TEST(Sha256Test, Sha256_Consistency)
{
    std::string const input = "consistent input string";
    auto const first  = sha256(input);
    auto const second = sha256(input);
    EXPECT_EQ(first, second);
}

// ---------------------------------------------------------------------------
// Test 5: distinct inputs produce distinct digests
// ---------------------------------------------------------------------------
TEST(Sha256Test, Sha256_DifferentInputsDifferentOutput)
{
    auto const d1 = sha256("input_one");
    auto const d2 = sha256("input_two");
    EXPECT_NE(d1, d2);
}

// ---------------------------------------------------------------------------
// Test 6: NIST FIPS 180-4 Appendix B.2 — 56-byte input, two-block padding path
// Expected: 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
// ---------------------------------------------------------------------------
TEST(Sha256Test, Sha256_TwoBlockPadding_NistAppendixB2)
{
    auto const result = sha256(
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    auto const expected = hex_to_bytes(
        "248d6a61d20638b8e5c026930c3e6039"
        "a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(result, expected);
}
