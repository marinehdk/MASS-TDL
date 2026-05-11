#include "m5_tactical_planner/common/sha256.hpp"

#include <cstdint>
#include <cstring>

namespace mass_l3::m5::common {

// ---------------------------------------------------------------------------
// NIST FIPS 180-4 SHA-256 constants
// ---------------------------------------------------------------------------

static constexpr uint32_t K[64] = {
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
[[nodiscard]] static uint32_t rotr32(uint32_t x, uint32_t n) noexcept
{
    return (x >> n) | (x << (32U - n));
}

// ---------------------------------------------------------------------------
// Helper: load first 16 words from block bytes (big-endian)
// ---------------------------------------------------------------------------
static void load_message_words(uint8_t const* block, uint32_t* w) noexcept
{
    for (uint32_t i = 0U; i < 16U; ++i) {
        uint32_t const base = i * 4U;
        w[i] = (static_cast<uint32_t>(block[base])      << 24U)
             | (static_cast<uint32_t>(block[base + 1U]) << 16U)
             | (static_cast<uint32_t>(block[base + 2U]) <<  8U)
             |  static_cast<uint32_t>(block[base + 3U]);
    }
}

// ---------------------------------------------------------------------------
// process_block: compress one 512-bit (64-byte) block into state[8]
// ---------------------------------------------------------------------------
static void process_block(std::array<uint32_t, 8>& state,
                          uint8_t const* block) noexcept
{
    uint32_t w[64]{};
    load_message_words(block, w);

    // Extend to 64 words via σ0 / σ1
    for (uint32_t i = 16U; i < 64U; ++i) {
        uint32_t const s0 = rotr32(w[i-15U],  7U) ^ rotr32(w[i-15U], 18U) ^ (w[i-15U] >>  3U);
        uint32_t const s1 = rotr32(w[i- 2U], 17U) ^ rotr32(w[i- 2U], 19U) ^ (w[i- 2U] >> 10U);
        w[i] = w[i-16U] + s0 + w[i-7U] + s1;
    }

    // Working variables
    uint32_t a = state[0]; uint32_t b = state[1];
    uint32_t c = state[2]; uint32_t d = state[3];
    uint32_t e = state[4]; uint32_t f = state[5];
    uint32_t g = state[6]; uint32_t h = state[7];

    // 64 rounds
    for (uint32_t i = 0U; i < 64U; ++i) {
        uint32_t const S1  = rotr32(e, 6U) ^ rotr32(e, 11U) ^ rotr32(e, 25U);
        uint32_t const ch  = (e & f) ^ ((~e) & g);
        uint32_t const t1  = h + S1 + ch + K[i] + w[i];
        uint32_t const S0  = rotr32(a, 2U) ^ rotr32(a, 13U) ^ rotr32(a, 22U);
        uint32_t const maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t const t2  = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

// ---------------------------------------------------------------------------
// Helper: write 64-bit bit-length as big-endian into buf (all 8 bytes)
// ---------------------------------------------------------------------------
static void write_bit_length_be(uint8_t* buf, std::size_t count_offset,
                                 std::uint64_t bit_len) noexcept
{
    buf[count_offset    ] = static_cast<uint8_t>((bit_len >> 56U) & 0xffU);
    buf[count_offset + 1U] = static_cast<uint8_t>((bit_len >> 48U) & 0xffU);
    buf[count_offset + 2U] = static_cast<uint8_t>((bit_len >> 40U) & 0xffU);
    buf[count_offset + 3U] = static_cast<uint8_t>((bit_len >> 32U) & 0xffU);
    buf[count_offset + 4U] = static_cast<uint8_t>((bit_len >> 24U) & 0xffU);
    buf[count_offset + 5U] = static_cast<uint8_t>((bit_len >> 16U) & 0xffU);
    buf[count_offset + 6U] = static_cast<uint8_t>((bit_len >>  8U) & 0xffU);
    buf[count_offset + 7U] = static_cast<uint8_t>( bit_len         & 0xffU);
}

// ---------------------------------------------------------------------------
// Helper: produce big-endian 32-byte digest from state words
// ---------------------------------------------------------------------------
[[nodiscard]] static std::array<uint8_t, 32>
produce_digest(std::array<uint32_t, 8> const& state) noexcept
{
    std::array<uint8_t, 32> digest{};
    for (uint32_t i = 0U; i < 8U; ++i) {
        uint32_t const base = i * 4U;
        digest[base    ] = static_cast<uint8_t>(state[i] >> 24U);
        digest[base + 1U] = static_cast<uint8_t>(state[i] >> 16U);
        digest[base + 2U] = static_cast<uint8_t>(state[i] >>  8U);
        digest[base + 3U] = static_cast<uint8_t>(state[i]);
    }
    return digest;
}

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

    std::size_t const msg_len  = input.size();
    std::size_t const bit_len  = msg_len * 8U;

    // Process all full blocks directly from input
    std::size_t offset = 0U;
    while (offset + 64U <= msg_len) {
        process_block(state, reinterpret_cast<uint8_t const*>(input.data()) + offset);
        offset += 64U;
    }

    // Build padding in stack buffer (at most 2 blocks)
    uint8_t buf[128]{};
    std::size_t const tail_len = msg_len - offset;
    std::memcpy(buf, input.data() + offset, tail_len);
    buf[tail_len] = 0x80U;

    // Append 8-byte big-endian bit count at end of padding block(s)
    // If tail fits in one block (<= 55 bytes after data), use 1 block;
    // otherwise use 2 blocks (buf spans 128 bytes).
    std::size_t const count_offset = (tail_len < 56U) ? 56U : 120U;
    write_bit_length_be(buf, count_offset, static_cast<std::uint64_t>(bit_len));

    // Process final 1 or 2 padding blocks
    process_block(state, buf);
    if (tail_len >= 56U) {
        process_block(state, buf + 64U);
    }

    return produce_digest(state);
}

}  // namespace mass_l3::m5::common
