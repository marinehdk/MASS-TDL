// ===========================================================================
// L4 Contract Tests (7-layer regression baseline, spec §5 — VR-05).
//
// These tests exercise the L4 status-fail-closed mapping contract established
// by Task 3 (L4-T3). The contract: raw acados solver statuses MUST be mapped
// fail-closed — raw 4 (QP error during refinement) MUST NEVER become Converged,
// and the layered solver_status/safety_status fields MUST correctly separate
// solver outcome from safety assessment.
//
// Tests:
//   L4-T1: raw 4 never Converged — map_acados_status_to_solver_status(4) != Converged
//   L4-T2: raw 0 requires all conditions — the only Converged path
//   L4-T3: raw 1→Timeout, raw 2→Infeasible, raw 3→NumericalFailure
//   L4-T4: default MidMpcSolution construction — all status fields at safe defaults
//   L4-T5: backward-compatible status mapping from solver_status
//   L4-T6: safety_status precedence (Unsafe > Degraded > Nominal)
//   L4-T7: MidMpcSolution::Status enum values match spec
//   L4-T8: MidMpcSolution::SolverStatus enum values match spec
//   L4-T9: MidMpcSolution::SafetyStatus enum values match spec
// ===========================================================================

#include <gtest/gtest.h>

#include <cstdint>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp"

namespace {

using mass_l3::m5::MidMpcSolution;
using mass_l3::m5::mid_mpc::map_acados_status_to_solver_status;
using mass_l3::m5::mid_mpc::solver_status_to_status;

// ===========================================================================
// L4-T1: raw 4 never Converged (VR-05 — THE critical contract).
// The old code re-mapped raw 4 → Converged when solver_moved && csat. This was
// FAIL-OPEN: a QP error during refinement with unverified stationarity and
// complementarity was reported as Converged. The fix (VR-05) closes this path.
//
// map_acados_status_to_solver_status(4) MUST return QpRecovered, NOT Converged.
// The backward-compatible status (via solver_status_to_status) MUST return
// NumericalFailure, NOT Converged.
// ===========================================================================
TEST(L4ContractRaw4Test, Raw4_SolverStatus_NotConverged) {
  const auto ss = map_acados_status_to_solver_status(4);
  EXPECT_NE(ss, MidMpcSolution::SolverStatus::Converged)
      << "VR-05 contract: raw 4 MUST NOT map to Converged in solver_status. "
      << "Got Converged — this is the fail-open path that was CLOSED.";
  EXPECT_EQ(ss, MidMpcSolution::SolverStatus::QpRecovered)
      << "raw 4 should map to QpRecovered (primal feasible but stationarity/"
      << "complementarity unverified).";
}

TEST(L4ContractRaw4Test, Raw4_BackwardCompatStatus_NotConverged) {
  const auto ss = map_acados_status_to_solver_status(4);
  const auto status = solver_status_to_status(ss);
  EXPECT_NE(status, MidMpcSolution::Status::Converged)
      << "VR-05 contract: raw 4 MUST NOT result in Converged in the backward-"
      << "compatible status field. Downstream consumers that read `status` "
      << "must see NumericalFailure, NOT Converged.";
  EXPECT_EQ(status, MidMpcSolution::Status::NumericalFailure)
      << "QpRecovered → backward-compat status must be NumericalFailure "
      << "(fail-closed: downstream sees non-converged and triggers fallback).";
}

// ===========================================================================
// L4-T2: raw 0 is the ONLY Converged path.
// raw 0 (ACADOS_SUCCESS) means all KKT conditions were met — stationarity,
// complementarity, primal feasibility, and dual feasibility all independently
// passing. This is the only status that maps to Converged in both solver_status
// and backward-compat status.
// ===========================================================================
TEST(L4ContractRaw0Test, Raw0_SolverStatus_Converged) {
  const auto ss = map_acados_status_to_solver_status(0);
  EXPECT_EQ(ss, MidMpcSolution::SolverStatus::Converged)
      << "raw 0 (ACADOS_SUCCESS) is the ONLY Converged path. All KKT conditions "
      << "must be independently verified by the solver before returning raw 0.";
}

TEST(L4ContractRaw0Test, Raw0_BackwardCompatStatus_Converged) {
  const auto ss = map_acados_status_to_solver_status(0);
  const auto status = solver_status_to_status(ss);
  EXPECT_EQ(status, MidMpcSolution::Status::Converged)
      << "raw 0 → Converged → Converged through the backward-compat pipeline.";
}

// ===========================================================================
// L4-T3: raw 1→Timeout, raw 2→Infeasible, raw 3→NumericalFailure.
// Contract tests for each raw status to ensure the mapping is correct and
// nothing silently substitutes.
// ===========================================================================
TEST(L4ContractRawStatusTest, Raw1_MapsToTimeout) {
  const auto ss = map_acados_status_to_solver_status(1);
  EXPECT_EQ(ss, MidMpcSolution::SolverStatus::Timeout)
      << "raw 1 (max_iter hit) must map to Timeout in solver_status.";
  const auto status = solver_status_to_status(ss);
  EXPECT_EQ(status, MidMpcSolution::Status::Timeout)
      << "Timeout → backward-compat status must be Timeout.";
}

TEST(L4ContractRawStatusTest, Raw2_MapsToInfeasible) {
  const auto ss = map_acados_status_to_solver_status(2);
  EXPECT_EQ(ss, MidMpcSolution::SolverStatus::Infeasible)
      << "raw 2 (QP infeasible) must map to Infeasible in solver_status.";
  const auto status = solver_status_to_status(ss);
  EXPECT_EQ(status, MidMpcSolution::Status::Infeasible)
      << "Infeasible → backward-compat status must be Infeasible.";
}

TEST(L4ContractRawStatusTest, Raw3_MapsToNumericalFailure) {
  const auto ss = map_acados_status_to_solver_status(3);
  EXPECT_EQ(ss, MidMpcSolution::SolverStatus::NumericalFailure)
      << "raw 3 (QP solver failed) must map to NumericalFailure in solver_status.";
  const auto status = solver_status_to_status(ss);
  EXPECT_EQ(status, MidMpcSolution::Status::NumericalFailure)
      << "NumericalFailure → backward-compat status must be NumericalFailure.";
}

TEST(L4ContractRawStatusTest, Raw7_MapsToNumericalFailure) {
  // Unexpected raw status (e.g., 7 — undocumented acados code) → NumericalFailure.
  const auto ss = map_acados_status_to_solver_status(7);
  EXPECT_EQ(ss, MidMpcSolution::SolverStatus::NumericalFailure)
      << "Unexpected raw status must map to NumericalFailure (fail-closed).";
  const auto status = solver_status_to_status(ss);
  EXPECT_EQ(status, MidMpcSolution::Status::NumericalFailure);
}

TEST(L4ContractRawStatusTest, RawNegative_MapsToNumericalFailure) {
  // Negative raw status (should never occur, but must be fail-closed).
  const auto ss = map_acados_status_to_solver_status(-1);
  EXPECT_EQ(ss, MidMpcSolution::SolverStatus::NumericalFailure)
      << "Negative raw status must map to NumericalFailure (fail-closed).";
}

// ===========================================================================
// L4-T4: MidMpcSolution default construction — all status fields at safe defaults.
// A freshly default-constructed MidMpcSolution MUST have:
//   status         = NotInitialized
//   solver_status  = NotInitialized
//   safety_status  = Unknown
//   trajectory     = empty
//   rationale      = empty
// This ensures that an uninitialized solution cannot be mistaken for a valid one.
// ===========================================================================
TEST(L4ContractDefaultConstructionTest, AllStatusFieldsAtSafeDefaults) {
  MidMpcSolution sol;

  // Backward-compatible status must be NotInitialized.
  EXPECT_EQ(sol.status, MidMpcSolution::Status::NotInitialized)
      << "Default-constructed status must be NotInitialized (safe default).";

  // Layered solver_status must be NotInitialized.
  EXPECT_EQ(sol.solver_status, MidMpcSolution::SolverStatus::NotInitialized)
      << "Default-constructed solver_status must be NotInitialized.";

  // Layered safety_status must be Unknown.
  EXPECT_EQ(sol.safety_status, MidMpcSolution::SafetyStatus::Unknown)
      << "Default-constructed safety_status must be Unknown (safe default).";

  // Trajectory must be empty.
  EXPECT_TRUE(sol.trajectory.empty())
      << "Default-constructed trajectory must be empty.";

  // Rationale must be empty.
  EXPECT_TRUE(sol.rationale.empty())
      << "Default-constructed rationale must be empty.";

  // Cost fields must be zero.
  EXPECT_DOUBLE_EQ(sol.cost_total, 0.0);
  EXPECT_DOUBLE_EQ(sol.cost_colreg, 0.0);
  EXPECT_DOUBLE_EQ(sol.cost_dist, 0.0);
  EXPECT_DOUBLE_EQ(sol.cost_vel, 0.0);
}

// ===========================================================================
// L4-T5: backward-compatible status mapping from solver_status.
// Every SolverStatus value must map to a defined backward-compat Status.
// The critical contract: QpRecovered → NumericalFailure, NEVER Converged.
// ===========================================================================
TEST(L4ContractSolverToStatusTest, Converged_MapsToConverged) {
  EXPECT_EQ(solver_status_to_status(MidMpcSolution::SolverStatus::Converged),
            MidMpcSolution::Status::Converged);
}

TEST(L4ContractSolverToStatusTest, QpRecovered_MapsToNumericalFailure) {
  EXPECT_EQ(solver_status_to_status(MidMpcSolution::SolverStatus::QpRecovered),
            MidMpcSolution::Status::NumericalFailure)
      << "VR-05: QpRecovered MUST map to NumericalFailure, NOT Converged.";
}

TEST(L4ContractSolverToStatusTest, Timeout_MapsToTimeout) {
  EXPECT_EQ(solver_status_to_status(MidMpcSolution::SolverStatus::Timeout),
            MidMpcSolution::Status::Timeout);
}

TEST(L4ContractSolverToStatusTest, Infeasible_MapsToInfeasible) {
  EXPECT_EQ(solver_status_to_status(MidMpcSolution::SolverStatus::Infeasible),
            MidMpcSolution::Status::Infeasible);
}

TEST(L4ContractSolverToStatusTest, NumericalFailure_MapsToNumericalFailure) {
  EXPECT_EQ(solver_status_to_status(MidMpcSolution::SolverStatus::NumericalFailure),
            MidMpcSolution::Status::NumericalFailure);
}

TEST(L4ContractSolverToStatusTest, NotInitialized_MapsToNotInitialized) {
  EXPECT_EQ(solver_status_to_status(MidMpcSolution::SolverStatus::NotInitialized),
            MidMpcSolution::Status::NotInitialized);
}

// ===========================================================================
// L4-T6: safety_status precedence contract.
// The safety_status enum values encode precedence: Unsafe (2) > Degraded (1) >
// Nominal (0). Unknown (3) is the default sentinel. The solver must assign
// safety_status following the precedence:
//   1. D1 witness failure or NaN trajectory → Unsafe
//   2. L0 input degradation → Degraded
//   3. Otherwise → Nominal
// These tests verify the enum ordering and that the default is Unknown.
// ===========================================================================
TEST(L4ContractSafetyStatusTest, EnumPrecedenceOrder) {
  // Lower numeric value = safer. The precedence for escalation is:
  // Unsafe (2) > Degraded (1) > Nominal (0).
  EXPECT_LT(static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Nominal),
            static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Degraded))
      << "Nominal < Degraded in escalation precedence.";
  EXPECT_LT(static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Degraded),
            static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Unsafe))
      << "Degraded < Unsafe in escalation precedence.";

  // Unknown (3) is the sentinel default, distinct from all others.
  EXPECT_NE(static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Unknown),
            static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Nominal));
  EXPECT_NE(static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Unknown),
            static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Degraded));
  EXPECT_NE(static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Unknown),
            static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Unsafe));
}

// ===========================================================================
// L4-T7: MidMpcSolution::Status enum values match spec.
// The Status enum values are baked into the ROS2 message contract; they must
// not drift silently (a renumbering would break downstream L4/L5/M7 consumers
// that compare against integer status values in AvoidancePlan messages).
// ===========================================================================
TEST(L4ContractStatusEnumTest, StatusEnumValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::Status::Converged), 0u);
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::Status::Timeout), 1u);
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::Status::Infeasible), 2u);
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::Status::NumericalFailure), 3u);
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::Status::NotInitialized), 4u);
}

// ===========================================================================
// L4-T8: MidMpcSolution::SolverStatus enum values match spec.
// The SolverStatus enum separates solver outcome from safety assessment.
// ===========================================================================
TEST(L4ContractSolverStatusEnumTest, SolverStatusEnumValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::SolverStatus::Converged), 0u);
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::SolverStatus::QpRecovered), 1u);
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::SolverStatus::Timeout), 2u);
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::SolverStatus::Infeasible), 3u);
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::SolverStatus::NumericalFailure), 4u);
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::SolverStatus::NotInitialized), 5u);
}

// ===========================================================================
// L4-T9: MidMpcSolution::SafetyStatus enum values match spec.
// ===========================================================================
TEST(L4ContractSafetyStatusEnumTest, SafetyStatusEnumValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Nominal), 0u);
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Degraded), 1u);
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Unsafe), 2u);
  EXPECT_EQ(static_cast<std::uint8_t>(MidMpcSolution::SafetyStatus::Unknown), 3u);
}

// ===========================================================================
// L4-T10: Verify that solver_status_to_status rejects ALL Converged from
// QpRecovered. This is a defense-in-depth test — even if solver_status changes
// later, the mapping must not accidentally promote QpRecovered to Converged.
// ===========================================================================
TEST(L4ContractDefenseInDepthTest, NoPathFromQpRecoveredToConverged) {
  // Explicitly verify that for QpRecovered, the backward-compat status is
  // NEVER Converged, regardless of how the enum values might change.
  for (int i = 0; i <= 5; ++i) {
    const auto ss = static_cast<MidMpcSolution::SolverStatus>(i);
    const auto status = solver_status_to_status(ss);
    if (ss == MidMpcSolution::SolverStatus::Converged) {
      EXPECT_EQ(status, MidMpcSolution::Status::Converged)
          << "Only SolverStatus::Converged should map to Status::Converged.";
    } else {
      EXPECT_NE(status, MidMpcSolution::Status::Converged)
          << "SolverStatus " << i << " must NOT map to Status::Converged. "
          << "Got Converged — this is the fail-open path that VR-05 closed.";
    }
  }
}

}  // namespace
