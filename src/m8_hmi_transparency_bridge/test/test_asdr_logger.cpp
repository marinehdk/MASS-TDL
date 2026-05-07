#include <gtest/gtest.h>

#include "m8_hmi_transparency_bridge/asdr_logger.hpp"

namespace {

builtin_interfaces::msg::Time make_stamp(int32_t sec = 0, uint32_t nanosec = 0)
{
    builtin_interfaces::msg::Time t{};
    t.sec = sec;
    t.nanosec = nanosec;
    return t;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Test 1: build_record sets correct source_module, decision_type, decision_json
// ---------------------------------------------------------------------------
TEST(AsdrLogger, BuildRecord_CorrectFields)
{
    mass_l3::m8::AsdrLogger logger{};
    auto record = logger.build_record(make_stamp(1, 0), "colreg_action", "{\"rule\":16}");

    EXPECT_EQ(record.source_module, "M8_HMI_Transparency_Bridge");
    EXPECT_EQ(record.decision_type, "colreg_action");
    EXPECT_EQ(record.decision_json, "{\"rule\":16}");
}

// ---------------------------------------------------------------------------
// Test 2: SHA-256 signature is exactly 32 bytes
// ---------------------------------------------------------------------------
TEST(AsdrLogger, BuildRecord_SignatureIs32Bytes)
{
    mass_l3::m8::AsdrLogger logger{};
    auto record = logger.build_record(make_stamp(), "test_type", "{\"x\":1}");

    EXPECT_EQ(record.signature.size(), 32U);
}

// ---------------------------------------------------------------------------
// Test 3: same input produces same signature (deterministic)
// ---------------------------------------------------------------------------
TEST(AsdrLogger, BuildRecord_DeterministicSignature)
{
    mass_l3::m8::AsdrLogger logger{};
    auto r1 = logger.build_record(make_stamp(5, 0), "det_test", "{\"v\":42}");
    auto r2 = logger.build_record(make_stamp(5, 0), "det_test", "{\"v\":42}");

    EXPECT_EQ(r1.signature, r2.signature);
}

// ---------------------------------------------------------------------------
// Test 4: different decision_json produces different signature
// ---------------------------------------------------------------------------
TEST(AsdrLogger, BuildRecord_DifferentInput_DifferentSignature)
{
    mass_l3::m8::AsdrLogger logger{};
    auto r1 = logger.build_record(make_stamp(), "type_a", "{\"a\":1}");
    auto r2 = logger.build_record(make_stamp(), "type_a", "{\"a\":2}");

    EXPECT_NE(r1.signature, r2.signature);
}

// ---------------------------------------------------------------------------
// Test 5: build_tor_acknowledgment_record sets decision_type == "tor_acknowledged"
// ---------------------------------------------------------------------------
TEST(AsdrLogger, TorAcknowledgment_CorrectDecisionType)
{
    mass_l3::m8::AsdrLogger logger{};
    auto record = logger.build_tor_acknowledgment_record(
        make_stamp(10, 0),
        "operator_007",
        12.5,
        "OPEN_SEA",
        0.95F);

    EXPECT_EQ(record.decision_type, "tor_acknowledged");
    EXPECT_EQ(record.source_module, "M8_HMI_Transparency_Bridge");
    EXPECT_EQ(record.signature.size(), 32U);
    // JSON must contain operator_id
    EXPECT_NE(record.decision_json.find("operator_007"), std::string::npos);
    EXPECT_NE(record.decision_json.find("OPEN_SEA"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 6: build_ui_snapshot_record sets decision_type == "ui_state_snapshot"
// ---------------------------------------------------------------------------
TEST(AsdrLogger, UiSnapshot_CorrectDecisionType)
{
    mass_l3::m8::AsdrLogger logger{};
    auto record = logger.build_ui_snapshot_record(
        make_stamp(20, 500),
        "STANDBY_MONITORING");

    EXPECT_EQ(record.decision_type, "ui_state_snapshot");
    EXPECT_EQ(record.source_module, "M8_HMI_Transparency_Bridge");
    EXPECT_EQ(record.signature.size(), 32U);
    EXPECT_NE(record.decision_json.find("STANDBY_MONITORING"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 7: stamp is preserved in the record
// ---------------------------------------------------------------------------
TEST(AsdrLogger, BuildRecord_StampPreserved)
{
    mass_l3::m8::AsdrLogger logger{};
    auto stamp = make_stamp(999, 123456789U);
    auto record = logger.build_record(stamp, "stamp_test", "{}");

    EXPECT_EQ(record.stamp.sec, 999);
    EXPECT_EQ(record.stamp.nanosec, 123456789U);
}

// ---------------------------------------------------------------------------
// Test 8: Known SHA-256 vector -- empty string
// (SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855)
// ---------------------------------------------------------------------------
TEST(AsdrLogger, BuildRecord_KnownSha256EmptyString)
{
    mass_l3::m8::AsdrLogger logger{};
    auto record = logger.build_record(make_stamp(), "known_vector", "");

    static constexpr uint8_t expected[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
    };
    ASSERT_EQ(record.signature.size(), 32U);
    for (std::size_t i = 0; i < 32U; ++i) {
        EXPECT_EQ(record.signature[i], expected[i]) << "byte " << i;
    }
}
