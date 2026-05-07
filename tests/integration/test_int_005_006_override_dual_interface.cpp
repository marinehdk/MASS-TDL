// TDD RED: stub for INT-005 + INT-006 Hardware override + M5 dual interface
// INT-005 Scene: Mock OverrideActiveSignal → M1 enters OVERRIDDEN
//               → M5 freezes trajectory output within 100ms.
// INT-006 Scene: M5 tri-mode switch: normal_LOS → avoidance_planning
//               (on AvoidancePlan publish) → reactive_override
//               (on ReactiveOverrideCmd). Each switch <100ms.
//               Verify RFC-001 scheme B dual output interface.
#include <gtest/gtest.h>

TEST(IntegrationTest, INT005_HardwareOverride_Stub)
{
    FAIL() << "INT-005: not yet implemented";
}

TEST(IntegrationTest, INT006_M5DualInterface_Stub)
{
    FAIL() << "INT-006: not yet implemented";
}
