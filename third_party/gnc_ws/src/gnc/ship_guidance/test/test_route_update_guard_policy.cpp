#include "ship_guidance/route_update_guard_policy.hpp"

#include <gtest/gtest.h>

TEST(RouteUpdateGuardPolicy, KeepsIntervalGuardForNominalRoutes)
{
    EXPECT_TRUE(ship_guidance::should_enforce_route_update_interval(false, true));
    EXPECT_TRUE(ship_guidance::should_enforce_route_update_interval(false, false));
}

TEST(RouteUpdateGuardPolicy, BypassesIntervalGuardForRelaxedEmergencyAvoidance)
{
    EXPECT_FALSE(ship_guidance::should_enforce_route_update_interval(true, true));
}

TEST(RouteUpdateGuardPolicy, KeepsIntervalGuardWhenEmergencyRelaxationDisabled)
{
    EXPECT_TRUE(ship_guidance::should_enforce_route_update_interval(true, false));
}

TEST(RouteUpdateGuardPolicy, BypassesDynamicGuardForPostColregsRejoinWhenOnIncomingRoute)
{
    EXPECT_TRUE(ship_guidance::should_bypass_dynamic_update_guard_for_post_colregs_rejoin(
        false, true, 12.0, 100.0));
    EXPECT_TRUE(ship_guidance::should_bypass_dynamic_update_guard_for_post_colregs_rejoin(
        false, true, -99.9, 100.0));
}

TEST(RouteUpdateGuardPolicy, KeepsDynamicGuardForOrdinaryOrStillProtectedUpdates)
{
    EXPECT_FALSE(ship_guidance::should_bypass_dynamic_update_guard_for_post_colregs_rejoin(
        false, false, 12.0, 100.0));
    EXPECT_FALSE(ship_guidance::should_bypass_dynamic_update_guard_for_post_colregs_rejoin(
        true, true, 12.0, 100.0));
    EXPECT_FALSE(ship_guidance::should_bypass_dynamic_update_guard_for_post_colregs_rejoin(
        false, true, 101.0, 100.0));
}
