#include <gtest/gtest.h>

#include "m4_behavior_arbiter/asdr_logger.hpp"
#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {
namespace {

// L1: BehaviorChangeLogs_WhenDifferent
TEST(AsdrLoggerTest, BehaviorChangeLogs_WhenDifferent) {
  AsdrLogger logger;
  // Should not throw
  logger.log_behavior_change(BehaviorType::Transit, BehaviorType::ColregAvoid,
                             static_cast<uint8_t>(0), "test");
}

// L2: BehaviorChangeSilent_WhenSame
TEST(AsdrLoggerTest, BehaviorChangeSilent_WhenSame) {
  AsdrLogger logger;
  // Should not throw; log is a no-op when prev == curr
  logger.log_behavior_change(BehaviorType::Transit, BehaviorType::Transit,
                             static_cast<uint8_t>(0), "test");
}

// L3: IvpFailureLogs
TEST(AsdrLoggerTest, IvpFailureLogs) {
  AsdrLogger logger;
  // Should not throw
  logger.log_ivp_failure(2, static_cast<uint8_t>(0));
}

// L4: BehaviorChangeFromMrcDriftToTransit
TEST(AsdrLoggerTest, BehaviorChangeFromMrcDriftToTransit) {
  AsdrLogger logger;
  // Should not throw
  logger.log_behavior_change(BehaviorType::MrcDrift, BehaviorType::Transit,
                             static_cast<uint8_t>(1), "r");
}

// L5: BehaviorChangeAllZones
TEST(AsdrLoggerTest, BehaviorChangeAllZones) {
  AsdrLogger logger;
  // Test all zone values (0, 1, 2, 3)
  for (uint8_t zone = 0; zone < 4; ++zone) {
    logger.log_behavior_change(BehaviorType::ColregAvoid, BehaviorType::DpHold,
                               zone, "zone_test");
  }
}

// L6: IvpFailureZeroActive
TEST(AsdrLoggerTest, IvpFailureZeroActive) {
  AsdrLogger logger;
  // Should not throw
  logger.log_ivp_failure(0, static_cast<uint8_t>(0));
}

}  // namespace
}  // namespace mass_l3::m4
