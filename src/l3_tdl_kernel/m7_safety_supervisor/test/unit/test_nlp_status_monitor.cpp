#include <gtest/gtest.h>

#include <cstddef>

#include "l3_msgs/msg/avoidance_plan.hpp"
#include "m7_safety_supervisor/iec61508/fault_monitor.hpp"
#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"

namespace {

constexpr std::size_t assumption_index(mass_l3::m7::sotif::AssumptionId id)
{
  return static_cast<std::size_t>(id);
}

}  // namespace

TEST(NlpStatusMonitor, NonConvergedThreeTimesEscalatesSotif)
{
  mass_l3::m7::sotif::AssumptionMonitor monitor{mass_l3::m7::sotif::AssumptionConfig{}};

  EXPECT_FALSE(monitor.check_nlp_convergence(
      l3_msgs::msg::AvoidancePlan::NLP_RESTOREMENT_FAILED, 10.0F, false));
  EXPECT_FALSE(monitor.check_nlp_convergence(
      l3_msgs::msg::AvoidancePlan::NLP_RESTOREMENT_FAILED, 10.0F, false));
  EXPECT_TRUE(monitor.check_nlp_convergence(
      l3_msgs::msg::AvoidancePlan::NLP_RESTOREMENT_FAILED, 10.0F, false));
}

TEST(NlpStatusMonitor, TailGateFailureEscalatesImmediately)
{
  mass_l3::m7::sotif::AssumptionMonitor monitor{mass_l3::m7::sotif::AssumptionConfig{}};

  EXPECT_TRUE(monitor.check_nlp_convergence(
      l3_msgs::msg::AvoidancePlan::NLP_CONVERGED, 0.0F, true));
}

TEST(NlpStatusMonitor, ConvergedStatusResetsFailureCounter)
{
  mass_l3::m7::sotif::AssumptionMonitor monitor{mass_l3::m7::sotif::AssumptionConfig{}};

  EXPECT_FALSE(monitor.check_nlp_convergence(
      l3_msgs::msg::AvoidancePlan::NLP_NONCONVERGED, 10.0F, false));
  EXPECT_FALSE(monitor.check_nlp_convergence(
      l3_msgs::msg::AvoidancePlan::NLP_NONCONVERGED, 10.0F, false));
  EXPECT_FALSE(monitor.check_nlp_convergence(
      l3_msgs::msg::AvoidancePlan::NLP_CONVERGED, 0.0F, false));
  EXPECT_FALSE(monitor.check_nlp_convergence(
      l3_msgs::msg::AvoidancePlan::NLP_NONCONVERGED, 10.0F, false));
}

TEST(NlpStatusMonitor, AssumptionStatusIncludesNlpViolationSlot)
{
  mass_l3::m7::sotif::AssumptionMonitor monitor{mass_l3::m7::sotif::AssumptionConfig{}};

  (void)monitor.check_nlp_convergence(
      l3_msgs::msg::AvoidancePlan::NLP_MAX_ITER, 10.0F, false);
  (void)monitor.check_nlp_convergence(
      l3_msgs::msg::AvoidancePlan::NLP_MAX_ITER, 10.0F, false);
  auto status = monitor.evaluate(l3_msgs::msg::ODDState{}, l3_msgs::msg::WorldState{},
                                 l3_msgs::msg::COLREGsConstraint{}, 0.0,
                                 mass_l3::m7::sotif::CommLinkState{},
                                 std::chrono::steady_clock::now());
  EXPECT_FALSE(status.violation_active[assumption_index(mass_l3::m7::sotif::AssumptionId::kNlpConvergence)]);

  (void)monitor.check_nlp_convergence(
      l3_msgs::msg::AvoidancePlan::NLP_MAX_ITER, 10.0F, false);
  status = monitor.evaluate(l3_msgs::msg::ODDState{}, l3_msgs::msg::WorldState{},
                            l3_msgs::msg::COLREGsConstraint{}, 0.0,
                            mass_l3::m7::sotif::CommLinkState{},
                            std::chrono::steady_clock::now());

  EXPECT_TRUE(status.violation_active[assumption_index(mass_l3::m7::sotif::AssumptionId::kNlpConvergence)]);
  EXPECT_EQ(status.total_violation_count, 1U);
}

TEST(NlpStatusMonitor, FaultMonitorConsumesNlpStatusForDiagnosticCoverage)
{
  mass_l3::m7::iec61508::FaultMonitor monitor;

  EXPECT_TRUE(monitor.observe_nlp_status(
      l3_msgs::msg::AvoidancePlan::NLP_CONVERGED, 0.0F, false));
  EXPECT_FALSE(monitor.observe_nlp_status(
      l3_msgs::msg::AvoidancePlan::NLP_RESTOREMENT_FAILED, 10.0F, false));
  EXPECT_GT(monitor.fault_count(), 0U);
}
