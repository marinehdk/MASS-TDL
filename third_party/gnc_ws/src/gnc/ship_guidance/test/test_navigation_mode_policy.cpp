#include "ship_guidance/navigation_mode_policy.hpp"

#include <gtest/gtest.h>

TEST(NavigationModePolicy, ColregsOvertakeHasDedicatedCode)
{
    EXPECT_EQ(ship_guidance::navigation_mode_code("colregs_overtake"), 7);
    EXPECT_EQ(ship_guidance::navigation_mode_from_code(7.0), "colregs_overtake");
}

TEST(NavigationModePolicy, ColregsOvertakeIsProtectedButNotEmergency)
{
    EXPECT_TRUE(ship_guidance::is_colregs_protected_mode("colregs_overtake"));
    EXPECT_FALSE(ship_guidance::is_emergency_avoidance_mode("colregs_overtake"));
}

TEST(NavigationModePolicy, EmergencyAvoidanceRemainsProtectedAndEmergency)
{
    EXPECT_TRUE(ship_guidance::is_colregs_protected_mode("emergency_avoidance"));
    EXPECT_TRUE(ship_guidance::is_emergency_avoidance_mode("emergency_avoidance"));
}
