#include "m7_safety_supervisor/common/sha256.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace mass_l3::m7::common {

namespace {

// ---------------------------------------------------------------------------
// NIST FIPS 180-4 SHA-256 constants
// ---------------------------------------------------------------------------
// NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays): FIPS 180-4 fixed-size constant table
constexpr uint32_t kK[64] = { // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

// ---------------------------------------------------------------------------
// Helper: right-rotate 32-bit word
// ---------------------------------------------------------------------------
[[nodiscard]] uint32_t rotr32(uint32_t x, uint32_t n) noexcept
{
    return (x >> n) | (x << (32U - n));
}

// ---------------------------------------------------------------------------
// Helper: load first 16 words from block bytes (big-endian)
// ---------------------------------------------------------------------------
void load_message_words(uint8_t const* block, uint32_t* w) noexcept // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
{
    for (uint32_t i = 0U; i < 16U; ++i) {
        uint32_t const kBase = i * 4U;
        w[i] = (static_cast<uint32_t>(block[kBase])      << 24U) // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
             | (static_cast<uint32_t>(block[kBase + 1U]) << 16U) // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
             | (static_cast<uint32_t>(block[kBase + 2U]) <<  8U) // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
             |  static_cast<uint32_t>(block[kBase + 3U]);         // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
}

// ---------------------------------------------------------------------------
// Helper: schedule expansion (σ0/σ1) — extend 16 words to 64
// ---------------------------------------------------------------------------
void expand_schedule(uint32_t* w) noexcept // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
{
    for (uint32_t i = 16U; i < 64U; ++i) {
        uint32_t const kSig0 = rotr32(w[i-15U],  7U) ^ rotr32(w[i-15U], 18U) ^ (w[i-15U] >>  3U); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        uint32_t const kSig1 = rotr32(w[i- 2U], 17U) ^ rotr32(w[i- 2U], 19U) ^ (w[i- 2U] >> 10U); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        w[i] = w[i-16U] + kSig0 + w[i-7U] + kSig1; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
}

// ---------------------------------------------------------------------------
// Helper: one round of SHA-256 compression (inlined into loop)
// ---------------------------------------------------------------------------
void sha256_round(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d,
                  uint32_t& e, uint32_t& f, uint32_t& g, uint32_t& h,
                  uint32_t ki, uint32_t wi) noexcept
{
    // SHA-256 standard variable names (FIPS 180-4 §6.2.2)
    uint32_t const kS1  = rotr32(e, 6U) ^ rotr32(e, 11U) ^ rotr32(e, 25U);
    uint32_t const kCh  = (e & f) ^ ((~e) & g);
    uint32_t const kT1  = h + kS1 + kCh + ki + wi;
    uint32_t const kS0  = rotr32(a, 2U) ^ rotr32(a, 13U) ^ rotr32(a, 22U);
    uint32_t const kMaj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t const kT2  = kS0 + kMaj;
    h = g; g = f; f = e; e = d + kT1;
    d = c; c = b; b = a; a = kT1 + kT2;
}

// ---------------------------------------------------------------------------
// process_block: compress one 512-bit (64-byte) block into state[8]
// ---------------------------------------------------------------------------
void process_block(std::array<uint32_t, 8>& state,
                   uint8_t const* block) noexcept // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
{
    uint32_t w[64]{}; // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
    load_message_words(block, w); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    expand_schedule(w); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)

    uint32_t a = state[0]; uint32_t b = state[1];
    uint32_t c = state[2]; uint32_t d = state[3];
    uint32_t e = state[4]; uint32_t f = state[5];
    uint32_t g = state[6]; uint32_t h = state[7];

    for (uint32_t i = 0U; i < 64U; ++i) {
        sha256_round(a, b, c, d, e, f, g, h, kK[i], w[i]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

// ---------------------------------------------------------------------------
// Helper: write 64-bit bit-length as big-endian into buf (all 8 bytes)
// ---------------------------------------------------------------------------
void write_bit_length_be(uint8_t* buf, std::size_t count_offset, // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                         std::uint64_t bit_len) noexcept
{
    buf[count_offset    ] = static_cast<uint8_t>((bit_len >> 56U) & 0xffU); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    buf[count_offset + 1U] = static_cast<uint8_t>((bit_len >> 48U) & 0xffU); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    buf[count_offset + 2U] = static_cast<uint8_t>((bit_len >> 40U) & 0xffU); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    buf[count_offset + 3U] = static_cast<uint8_t>((bit_len >> 32U) & 0xffU); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    buf[count_offset + 4U] = static_cast<uint8_t>((bit_len >> 24U) & 0xffU); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    buf[count_offset + 5U] = static_cast<uint8_t>((bit_len >> 16U) & 0xffU); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    buf[count_offset + 6U] = static_cast<uint8_t>((bit_len >>  8U) & 0xffU); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    buf[count_offset + 7U] = static_cast<uint8_t>( bit_len         & 0xffU); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

// ---------------------------------------------------------------------------
// Helper: produce big-endian 32-byte digest from state words
// ---------------------------------------------------------------------------
[[nodiscard]] std::array<uint8_t, 32>
produce_digest(std::array<uint32_t, 8> const& state) noexcept
{
    std::array<uint8_t, 32> digest{};
    for (uint32_t i = 0U; i < 8U; ++i) {
        uint32_t const kBase = i * 4U;
        digest[kBase    ] = static_cast<uint8_t>(state[i] >> 24U);
        digest[kBase + 1U] = static_cast<uint8_t>(state[i] >> 16U);
        digest[kBase + 2U] = static_cast<uint8_t>(state[i] >>  8U);
        digest[kBase + 3U] = static_cast<uint8_t>(state[i]);
    }
    return digest;
}

}  // namespace

// ---------------------------------------------------------------------------
// sha256: pad message, iterate blocks, finalize
// Uses stack buffer (max 2 blocks = 128 bytes).
// ---------------------------------------------------------------------------
[[nodiscard]] std::array<uint8_t, 32>
sha256(std::string_view input) noexcept
{
    // Initial hash values H0–H7 (FIPS 180-4 §5.3.3)
    std::array<uint32_t, 8> state = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };

    std::size_t const kMsgLen  = input.size();
    std::size_t const kBitLen  = kMsgLen * 8U;

    // Process all full blocks directly from input
    std::size_t offset = 0U;
    while (offset + 64U <= kMsgLen) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic)
        process_block(state, reinterpret_cast<uint8_t const*>(input.data()) + offset);
        offset += 64U;
    }

    // Build padding in stack buffer (at most 2 blocks)
    uint8_t buf[128]{}; // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
    std::size_t const kTailLen = kMsgLen - offset;
    std::memcpy(buf, input.data() + offset, kTailLen); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    buf[kTailLen] = 0x80U;

    // Append 8-byte big-endian bit count at end of padding block(s)
    std::size_t const kCountOffset = (kTailLen < 56U) ? 56U : 120U;
    write_bit_length_be(buf, kCountOffset, static_cast<std::uint64_t>(kBitLen)); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)

    // Process final 1 or 2 padding blocks
    process_block(state, buf); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    if (kTailLen >= 56U) {
        process_block(state, buf + 64U); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay,cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }

    return produce_digest(state);
}

}  // namespace mass_l3::m7::common
