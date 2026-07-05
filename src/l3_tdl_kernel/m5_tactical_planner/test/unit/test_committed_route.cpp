#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "m5_tactical_planner/committed_route/committed_route.hpp"

using mass_l3::m5::committed_route::CommittedAvoidanceRoute;
using mass_l3::m5::committed_route::CommittedRouteCandidate;
using mass_l3::m5::committed_route::GeoWP;
using mass_l3::m5::committed_route::LifecycleState;
using mass_l3::m5::committed_route::lifecycle_state_name;

namespace {

// Route fixtures use realistic WGS84 lat/lon (degrees) per spec §3.7 GeoWP
// coordinate contract — GeoWP.lat_deg/lon_deg hold WGS84 degrees, NOT NED metres.
// Base near Imazu: lat≈34.0, lon≈130.0. ~0.001 deg ≈ 111 m latitude.
std::vector<GeoWP> route_a()
{
  return {
      GeoWP{34.00000, 130.00000, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{34.00090, 130.00018, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{34.00180, 130.00018, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{34.00270, 130.00000, 5.0, "REJOIN_TO_L2"}};
}

std::vector<GeoWP> route_b_with_same_prefix()
{
  return {
      GeoWP{34.00000, 130.00000, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{34.00090, 130.00018, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{34.00198, 130.00031, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{34.00306, 130.00000, 5.0, "REJOIN_TO_L2"}};
}

CommittedRouteCandidate candidate(
    std::string plan_id,
    std::vector<GeoWP> geometry,
    std::size_t frozen_prefix_count,
    double valid_until_s,
    bool nlp_ok = true)
{
  CommittedRouteCandidate c;
  c.plan_id = std::move(plan_id);
  c.geometry = std::move(geometry);
  c.frozen_prefix_count = frozen_prefix_count;
  c.valid_until_s = valid_until_s;
  c.nlp_ok = nlp_ok;
  return c;
}

}  // namespace

TEST(CommittedAvoidanceRoute, keeps_committed_prefix_when_suffix_revised)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  const std::uint32_t first_revision = manager.current().revision;

  ASSERT_TRUE(manager.try_revise(candidate("plan-b", route_b_with_same_prefix(), 2U, 30.0), 5.0));

  const auto& committed = manager.current();
  EXPECT_EQ(committed.revision, first_revision + 1U);
  ASSERT_EQ(committed.committed_prefix.size(), 2U);
  ASSERT_EQ(committed.active_geometry.size(), 4U);
  EXPECT_DOUBLE_EQ(committed.active_geometry[0].lat_deg, 34.00000);
  EXPECT_DOUBLE_EQ(committed.active_geometry[0].lon_deg, 130.00000);
  EXPECT_DOUBLE_EQ(committed.active_geometry[1].lat_deg, 34.00090);
  EXPECT_DOUBLE_EQ(committed.active_geometry[1].lon_deg, 130.00018);
  EXPECT_DOUBLE_EQ(committed.active_geometry[2].lat_deg, 34.00198);
  EXPECT_DOUBLE_EQ(committed.active_geometry[2].lon_deg, 130.00031);
}

TEST(CommittedAvoidanceRoute, repeated_geometry_refreshes_without_revision_bump)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  const std::uint32_t first_revision = manager.current().revision;
  const std::uint32_t first_hash = manager.current().route_hash;

  ASSERT_TRUE(manager.try_revise(candidate("plan-a-repeat", route_a(), 2U, 35.0), 10.0));

  EXPECT_EQ(manager.current().revision, first_revision);
  EXPECT_EQ(manager.current().route_hash, first_hash);
  EXPECT_EQ(manager.current().plan_id, "plan-a-repeat");
  EXPECT_DOUBLE_EQ(manager.current().valid_until_s, 35.0);
}

TEST(CommittedAvoidanceRoute, heartbeat_refreshes_valid_until_without_revision_bump)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  const std::uint32_t first_revision = manager.current().revision;
  const std::uint32_t first_hash = manager.current().route_hash;

  ASSERT_TRUE(manager.heartbeat("plan-a-heartbeat", 55.0, 12.0));

  EXPECT_EQ(manager.current().revision, first_revision);
  EXPECT_EQ(manager.current().route_hash, first_hash);
  EXPECT_EQ(manager.current().plan_id, "plan-a-heartbeat");
  EXPECT_DOUBLE_EQ(manager.current().valid_until_s, 55.0);
  EXPECT_EQ(manager.current().state, LifecycleState::Committed);
}

TEST(CommittedAvoidanceRoute, stale_keep_last_over_45_seconds_enters_degraded_hold)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

  EXPECT_FALSE(manager.should_enter_degraded_hold(45.0));
  EXPECT_TRUE(manager.should_enter_degraded_hold(45.001));
  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  EXPECT_EQ(manager.current().safety_concern_event, "committed_route_stale_gt_45s");
  EXPECT_EQ(manager.current().active_geometry.size(), route_a().size());
}

TEST(CommittedAvoidanceRoute, three_consecutive_nlp_failures_enter_degraded_hold)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

  EXPECT_FALSE(manager.try_revise(candidate("fail-1", route_b_with_same_prefix(), 2U, 30.0, false), 1.0));
  EXPECT_FALSE(manager.should_enter_degraded_hold(1.0));
  EXPECT_FALSE(manager.try_revise(candidate("fail-2", route_b_with_same_prefix(), 2U, 30.0, false), 2.0));
  EXPECT_FALSE(manager.should_enter_degraded_hold(2.0));
  EXPECT_FALSE(manager.try_revise(candidate("fail-3", route_b_with_same_prefix(), 2U, 30.0, false), 3.0));

  EXPECT_TRUE(manager.should_enter_degraded_hold(3.0));
  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  // v2.2 §13.2: event tag distinguishes the no-BC-MPC path (renamed from the
  // v2.1 "nlp_consecutive_failures_ge_3"). When BC-MPC is not taking over, the
  // escalation goes to DegradedHold + MRM-02 (SOTIF fail-safe).
  EXPECT_EQ(manager.current().safety_concern_event,
            "nlp_consecutive_failures_ge_3_no_bcmpc");
  EXPECT_EQ(manager.current().revision, 1U);
  EXPECT_EQ(manager.current().active_geometry.size(), route_a().size());
}

// Phase 2.2 (R1, spec v2.3 §13.5): a persistent NLP solver Infeasible that
// drives plan.status=DEGRADED never reaches the optimized try_revise path
// (corridor branch instead). The legacy commit counter (incremented only
// inside optimized try_revise on candidate.nlp_ok=false) could not cross 3
// through that path, so DegradedHold was unreachable for the dominant
// failure mode. The SOLVER counter (fed via notify_solver_consecutive_failures
// + try_revise 3rd arg) closes this gap.
TEST(CommittedAvoidanceRoute, solver_consecutive_failures_escalates_even_without_optimized_try_revise)
{
  CommittedAvoidanceRoute manager;
  // First commit succeeds.
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  // Simulate NLP Infeasible: every cycle the solver counter grows but the
  // optimized try_revise path is NOT called (plan.status=DEGRADED → corridor).
  // Only notify_solver_consecutive_failures + should_enter_degraded_hold run.
  manager.notify_solver_consecutive_failures(1U);
  EXPECT_FALSE(manager.should_enter_degraded_hold(1.0));
  manager.notify_solver_consecutive_failures(2U);
  EXPECT_FALSE(manager.should_enter_degraded_hold(2.0));
  manager.notify_solver_consecutive_failures(3U);
  EXPECT_TRUE(manager.should_enter_degraded_hold(3.0))
      << "solver counter crossing 3 must escalate even without optimized try_revise";
  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  EXPECT_EQ(manager.current().safety_concern_event,
            "nlp_consecutive_failures_ge_3_no_bcmpc");
}

TEST(CommittedAvoidanceRoute, solver_counter_below_3_does_not_escalate)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  manager.notify_solver_consecutive_failures(2U);
  EXPECT_FALSE(manager.should_enter_degraded_hold(1.0))
      << "solver counter < 3 must not escalate";
}

TEST(CommittedAvoidanceRoute, try_revise_takes_max_of_solver_and_commit_counters)
{
  // If solver counter is 3 but commit counter is 0 (no candidate.nlp_ok=false
  // rejected yet), an optimized try_revise with nlp_ok=false should escalate
  // immediately on the first call because the SOLVER counter is already 3.
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  EXPECT_FALSE(manager.try_revise(
      candidate("fail-1", route_b_with_same_prefix(), 2U, 30.0, false),
      1.0, /*solver_consecutive_failures=*/3U));
  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold)
      << "solver counter=3 should escalate on the first nlp_ok=false try_revise";
}

// ---------------------------------------------------------------------------
// Review Critical (spec §5.3/§14.3): a TailBuilder reject marks the candidate
// nlp_tail_gate_failed, and the node wires committed_candidate_from_plan with
// nlp_ok = !nlp_tail_gate_failed = false. The manager must NOT commit such a
// candidate as a fresh route; it must KeepLast (honest degradation) rather than
// publish a broken-tail route. This test pins the nlp_ok=false → KeepLast
// contract that the node's reject path depends on (the off-by-true bug passed
// nlp_ok=true on reject, so the broken tail got committed).
// ---------------------------------------------------------------------------
TEST(CommittedAvoidanceRoute, tailGateRejectCandidateKeepsLastInsteadOfCommitting)
{
  CommittedAvoidanceRoute manager;
  // An NLP-healthy route is committed first.
  ASSERT_TRUE(manager.try_revise(candidate("plan-healthy", route_a(), 2U, 20.0), 0.0));
  const std::uint32_t healthy_revision = manager.current().revision;

  // A revised route whose tail gate failed arrives with nlp_ok=false (what the
  // node must pass when plan.nlp_tail_gate_failed is true). It must NOT commit.
  EXPECT_FALSE(manager.try_revise(
      candidate("plan-tail-reject", route_b_with_same_prefix(), 2U, 30.0, /*nlp_ok=*/false), 1.0));
  EXPECT_EQ(manager.current().state, LifecycleState::KeepLast);
  // The previously committed healthy route is preserved (KeepLast), not the
  // broken-tail candidate.
  EXPECT_EQ(manager.current().revision, healthy_revision);
  EXPECT_EQ(manager.current().active_geometry.size(), route_a().size());
}

TEST(CommittedAvoidanceRoute, heartbeat_does_not_clear_degraded_hold_after_stale_gate)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  const std::uint32_t first_revision = manager.current().revision;
  const std::uint32_t first_hash = manager.current().route_hash;

  ASSERT_TRUE(manager.should_enter_degraded_hold(45.001));
  const double stale_started_at = manager.current().stale_committed_at_s;

  EXPECT_FALSE(manager.heartbeat("plan-a-heartbeat", 90.0, 50.0));

  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  EXPECT_EQ(manager.current().safety_concern_event, "committed_route_stale_gt_45s");
  EXPECT_EQ(manager.current().revision, first_revision);
  EXPECT_EQ(manager.current().route_hash, first_hash);
  EXPECT_DOUBLE_EQ(manager.current().stale_committed_at_s, stale_started_at);
  EXPECT_DOUBLE_EQ(manager.current().valid_until_s, 20.0);
}


TEST(CommittedAvoidanceRoute, valid_revised_route_can_exit_degraded_hold)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  ASSERT_TRUE(manager.should_enter_degraded_hold(45.001));

  EXPECT_TRUE(manager.try_revise(candidate("plan-b", route_b_with_same_prefix(), 2U, 80.0), 50.0));

  EXPECT_EQ(manager.current().state, LifecycleState::Committed);
  EXPECT_TRUE(manager.current().safety_concern_event.empty());
  EXPECT_EQ(manager.current().plan_id, "plan-b");
  EXPECT_DOUBLE_EQ(manager.current().valid_until_s, 80.0);
  EXPECT_EQ(manager.current().revision, 2U);
}

TEST(CommittedAvoidanceRoute, invalid_revisions_do_not_clear_degraded_hold)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  ASSERT_TRUE(manager.should_enter_degraded_hold(45.001));
  const std::uint32_t degraded_revision = manager.current().revision;
  const std::uint32_t degraded_hash = manager.current().route_hash;
  const double stale_started_at = manager.current().stale_committed_at_s;
  const std::string degraded_event = manager.current().safety_concern_event;

  EXPECT_FALSE(manager.try_revise(candidate("empty", {}, 0U, 80.0), 50.0));
  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  EXPECT_EQ(manager.current().safety_concern_event, degraded_event);
  EXPECT_EQ(manager.current().revision, degraded_revision);
  EXPECT_EQ(manager.current().route_hash, degraded_hash);
  EXPECT_DOUBLE_EQ(manager.current().stale_committed_at_s, stale_started_at);
  EXPECT_EQ(manager.current().plan_id, "plan-a");

  auto bad_label = route_b_with_same_prefix();
  bad_label[2].nav_mode = "MID_MPC";
  EXPECT_FALSE(manager.try_revise(candidate("bad-label", bad_label, 2U, 85.0), 51.0));
  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  EXPECT_EQ(manager.current().safety_concern_event, degraded_event);
  EXPECT_EQ(manager.current().revision, degraded_revision);
  EXPECT_EQ(manager.current().route_hash, degraded_hash);
  EXPECT_DOUBLE_EQ(manager.current().stale_committed_at_s, stale_started_at);
  EXPECT_EQ(manager.current().plan_id, "plan-a");

  auto conflicting = route_b_with_same_prefix();
  conflicting[0].lat_deg = 34.00009;  // > 1e-7 deg → not same waypoint → prefix conflict
  EXPECT_FALSE(manager.try_revise(candidate("prefix-conflict", conflicting, 2U, 90.0), 52.0));
  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  EXPECT_EQ(manager.current().safety_concern_event, degraded_event);
  EXPECT_EQ(manager.current().revision, degraded_revision);
  EXPECT_EQ(manager.current().route_hash, degraded_hash);
  EXPECT_DOUBLE_EQ(manager.current().stale_committed_at_s, stale_started_at);
  EXPECT_EQ(manager.current().plan_id, "plan-a");

  ASSERT_TRUE(manager.try_revise(candidate("plan-b", route_b_with_same_prefix(), 2U, 95.0), 53.0));
  EXPECT_EQ(manager.current().state, LifecycleState::Committed);
  EXPECT_TRUE(manager.current().safety_concern_event.empty());
  EXPECT_EQ(manager.current().plan_id, "plan-b");
  EXPECT_EQ(manager.current().revision, degraded_revision + 1U);
}

// Spec §6.6.3: prefix_count = requested (NOT max(existing, requested)).
// The committed prefix must shrink when a smaller frozen_prefix_count is
// requested (own-ship has pruned/overrun earlier waypoints). The legacy
// max-only behavior is removed.
TEST(CommittedAvoidanceRoute, committed_prefix_shrinks_when_smaller_prefix_requested)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  ASSERT_EQ(manager.current().committed_prefix.size(), 2U);

  // Revise geometry (new hash → geometry_changed) requesting a SMALLER prefix.
  ASSERT_TRUE(manager.try_revise(candidate("plan-b", route_b_with_same_prefix(), 0U, 30.0), 5.0));

  // prefix_count = requested (0), not max(2, 0). committed_prefix must be empty.
  EXPECT_EQ(manager.current().committed_prefix.size(), 0U);
  EXPECT_EQ(manager.current().active_geometry.size(), 4U);
}

// Spec §6.6.3 (Critical-3 review fix): the committed prefix must shrink on a
// SAME-GEOMETRY revise when a smaller frozen_prefix_count is requested. The
// legacy code only reassigned committed_prefix inside the geometry_changed
// block, so a same-hash revise (e.g. a heartbeat-rate refresh of the identical
// route after own advanced and pruned) left the stale larger prefix in place.
// Here the route is IDENTICAL (same hash) but the candidate requests prefix=1
// after an initial prefix=2 → the prefix must shrink to 1 with NO revision bump.
TEST(CommittedAvoidanceRoute, committed_prefix_shrinks_on_same_geometry_smaller_count)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  ASSERT_EQ(manager.current().committed_prefix.size(), 2U);
  const std::uint32_t first_revision = manager.current().revision;
  const std::uint32_t first_hash = manager.current().route_hash;

  // Identical geometry (route_a), new plan_id, SMALLER requested prefix (2 → 1).
  ASSERT_TRUE(manager.try_revise(candidate("plan-a-pruned", route_a(), 1U, 30.0), 5.0));

  // No geometry change → no revision/hash bump, but the prefix MUST shrink.
  EXPECT_EQ(manager.current().revision, first_revision);
  EXPECT_EQ(manager.current().route_hash, first_hash);
  EXPECT_EQ(manager.current().plan_id, "plan-a-pruned");
  EXPECT_EQ(manager.current().committed_prefix.size(), 1U)
      << "same geometry + smaller frozen_prefix_count must prune the prefix";
  // The surviving prefix waypoint is geometry[0] (the route head is unchanged).
  EXPECT_DOUBLE_EQ(manager.current().committed_prefix[0].lat_deg, 34.00000);
}

TEST(CommittedAvoidanceRoute, prefix_conflict_on_genuinely_different_waypoint_is_rejected)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  ASSERT_EQ(manager.current().committed_prefix.size(), 2U);

  auto conflicting = route_b_with_same_prefix();
  conflicting[0].lat_deg = 34.00009;  // 9e-5 deg ≈ 10 m → outside 1e-7 tolerance
  EXPECT_FALSE(manager.try_revise(candidate("plan-conflict", conflicting, 2U, 40.0), 10.0));

  EXPECT_EQ(manager.current().state, LifecycleState::KeepLast);
  ASSERT_EQ(manager.current().committed_prefix.size(), 2U);
  EXPECT_DOUBLE_EQ(manager.current().active_geometry[0].lat_deg, 34.00000);
  EXPECT_DOUBLE_EQ(manager.current().active_geometry[1].lat_deg, 34.00090);
}

TEST(CommittedAvoidanceRoute, failed_nlp_risk_triggers_enter_degraded_hold_immediately)
{
  auto expect_failed_nlp_degraded_hold = [](CommittedRouteCandidate risky, const std::string& event) {
    CommittedAvoidanceRoute manager;
    ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

    EXPECT_FALSE(manager.try_revise(risky, 1.0));

    EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
    EXPECT_EQ(manager.current().safety_concern_event, event);
    EXPECT_EQ(manager.current().revision, 1U);
    EXPECT_EQ(manager.current().active_geometry.size(), route_a().size());
  };

  // Phase 2.1/2.3 (R2/R6, spec v2.3 §3.2): the commit gate now mirrors
  // tail-gate floor semantics. A candidate is hard-blocked only when the
  // target is OPENING (release/recovery) AND the achieved terminal CPA is
  // still below the hard floor — the true fail-safe case. The legacy gate
  // rejected any candidate with current range < cpa_hard regardless of phase,
  // which on rule14-ho approach blocked the very CPA-opening maneuver it was
  // supposed to author (optimized_committed_rejected × 790 in V2.3 phase 3b).
  auto hard_cpa = candidate("fail-hard-cpa", route_b_with_same_prefix(), 2U, 30.0, false);
  hard_cpa.current_cpa_m = 49.0;       // range inside hard floor
  hard_cpa.cpa_hard_m = 50.0;
  hard_cpa.target_opening = true;       // release/recovery phase
  hard_cpa.terminal_cpa_m = 30.0;       // candidate did not open enough clearance
  expect_failed_nlp_degraded_hold(hard_cpa, "terminal_cpa_below_hard_floor_on_release");

  // REMOVED (Codex WRONG ABSTRACTION): target_heading_change_gt_15deg and
  // cpa_drift_gt_20pct were removed from risk_trigger_event (the commit-block
  // path). A target maneuver / CPA drift is a reason to replan or invalidate
  // the keep-last snapshot, NOT to reject a fresh candidate. They retain
  // advisory slots in should_enter_degraded_hold (target_heading_trigger_ /
  // cpa_drift_trigger_ members); wiring those is future work. A candidate
  // with nlp_ok=false + no hard-block gate now goes through the nlp_ok==false
  // path (KeepLast on first failure, DegradedHold after 3 consecutive).
}

// A candidate with nlp_ok=false but NO hard-block risk (target_heading /
// cpa_drift fields set, but those are no longer block gates per Codex review)
// must be rejected (nlp not ok) but must NOT trigger the immediate
// DegradedHold that a hard-block risk would — it goes to KeepLast on first
// failure (consecutive_nlp_failures < 3).
TEST(CommittedAvoidanceRoute, nlp_failed_without_hard_risk_goes_keeplast_not_degraded)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

  // Candidate with target_heading_delta + cpa_drift set but nlp_ok=false and
  // current_cpa above hard floor — previously blocked by heading/drift gates,
  // now only nlp_ok=false applies.
  auto c = candidate("plan-heading", route_b_with_same_prefix(), 2U, 30.0, false);
  c.target_heading_delta_deg = 15.1;
  c.cpa_drift_fraction = 0.201;
  c.current_cpa_m = 100.0;   // well above cpa_hard
  c.cpa_hard_m = 50.0;

  EXPECT_FALSE(manager.try_revise(c, 1.0));
  // Not DegradedHold (no hard risk trigger); first nlp failure → KeepLast.
  EXPECT_EQ(manager.current().state, LifecycleState::KeepLast);
  EXPECT_EQ(manager.current().revision, 1U);
}

// Spec §3.7 / §10.1 "GeoWP 坐标契约": same_waypoint uses tolerance comparison
// (|Δlat|,|Δlon| < 1e-7 deg ≈ 1cm, |Δspeed| < 0.01 m/s). Exact double
// comparison is forbidden. Verified indirectly via preserves_committed_prefix:
// a revised geometry whose prefix waypoints differ only by sub-tolerance
// WGS84 jitter must be accepted (not rejected as a prefix conflict).
TEST(CommittedAvoidanceRoute, preserves_committed_prefix_accepts_sub_tolerance_jitter)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

  // Jitter well below 1e-7 deg tolerance — represents WGS84 float reprojection
  // noise, NOT a real geometry change.
  auto jittered = route_b_with_same_prefix();
  jittered[0].lat_deg += 1e-9;
  jittered[0].lon_deg += 1e-9;
  jittered[1].lat_deg += 5e-10;
  jittered[1].speed_mps += 0.001;  // < 0.01 m/s speed tolerance

  EXPECT_TRUE(manager.try_revise(candidate("plan-jitter", jittered, 2U, 30.0), 5.0));
  EXPECT_EQ(manager.current().state, LifecycleState::Committed);
}

// Spec §3.7: tolerance must REJECT points that exceed the threshold, so that
// a real ~10 m waypoint move is treated as a genuine geometry change (the
// hash changes) but a sub-1e-7 deg perturbation is a no-op.
TEST(CommittedAvoidanceRoute, preserves_committed_prefix_rejects_above_tolerance_lat_delta)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

  auto drifted = route_b_with_same_prefix();
  drifted[0].lat_deg += 1e-6;  // 1e-6 deg ≈ 11 cm > 1e-7 tolerance

  // With a 2U frozen prefix, the drifted prefix[0] is NOT the same waypoint
  // → preserves_committed_prefix fails → KeepLast rejection.
  EXPECT_FALSE(manager.try_revise(candidate("plan-drift", drifted, 2U, 30.0), 5.0));
  EXPECT_EQ(manager.current().state, LifecycleState::KeepLast);
  EXPECT_EQ(manager.current().safety_concern_event, "frozen_prefix_conflict");
}

TEST(CommittedAvoidanceRoute, successful_empty_or_invalid_candidate_is_rejected_without_revision)
{
  CommittedAvoidanceRoute manager;
  EXPECT_FALSE(manager.try_revise(candidate("empty", {}, 0U, 20.0), 0.0));
  EXPECT_EQ(manager.current().revision, 0U);
  EXPECT_TRUE(manager.current().active_geometry.empty());
  EXPECT_EQ(manager.current().safety_concern_event, "candidate_preflight_failed");

  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 1.0));
  const std::uint32_t first_revision = manager.current().revision;
  const std::uint32_t first_hash = manager.current().route_hash;

  auto invalid = route_b_with_same_prefix();
  invalid[1].speed_mps = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(manager.try_revise(candidate("invalid", invalid, 2U, 30.0), 2.0));

  EXPECT_EQ(manager.current().revision, first_revision);
  EXPECT_EQ(manager.current().route_hash, first_hash);
  EXPECT_EQ(manager.current().state, LifecycleState::KeepLast);
  EXPECT_EQ(manager.current().safety_concern_event, "candidate_preflight_failed");
}

TEST(CommittedAvoidanceRoute, invented_source_label_is_rejected)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  const std::uint32_t first_revision = manager.current().revision;
  const std::uint32_t first_hash = manager.current().route_hash;

  auto invented_label = route_b_with_same_prefix();
  invented_label[2].nav_mode = "MID_MPC";
  EXPECT_FALSE(manager.try_revise(candidate("invented-label", invented_label, 2U, 30.0), 5.0));

  EXPECT_EQ(manager.current().revision, first_revision);
  EXPECT_EQ(manager.current().route_hash, first_hash);
  EXPECT_EQ(manager.current().state, LifecycleState::KeepLast);
  EXPECT_EQ(manager.current().safety_concern_event, "candidate_preflight_failed");
}

// v2.2 §13.2 (spec D3): LifecycleState 扩第九态 BcMpcFollow — committed_route
// 跟随 BC-MPC ReactiveOverrideCmd，不 KeepLast stale NLP corridor. v2 lifecycle
// 八态 (Idle..Released=7) 扩为九态，BcMpcFollow=8.
TEST(LifecycleStateV22, BcMpcFollowExists) {
  mass_l3::m5::committed_route::LifecycleState s =
      mass_l3::m5::committed_route::LifecycleState::BcMpcFollow;
  EXPECT_EQ(static_cast<std::uint8_t>(s), 8U);
}

// v2.2 §13.1/§13.2: when BC-MPC take-over has been signaled (MidMpcNode dispatch
// calls mark_bc_mpc_takeover() when solver consecutive_failures >= 3), three
// consecutive NLP failures must route the committed route into BcMpcFollow — NOT
// KeepLast (hold stale corridor) and NOT DegradedHold. BC-MPC owns the maneuver.
TEST(CommittedRouteV22, BcMpcFollowOnTakeoverRequest) {
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

  manager.mark_bc_mpc_takeover();  // MidMpcNode signals BC-MPC take-over

  EXPECT_FALSE(manager.try_revise(
      candidate("fail-1", route_b_with_same_prefix(), 2U, 30.0, false), 1.0));
  EXPECT_FALSE(manager.try_revise(
      candidate("fail-2", route_b_with_same_prefix(), 2U, 30.0, false), 2.0));
  EXPECT_FALSE(manager.try_revise(
      candidate("fail-3", route_b_with_same_prefix(), 2U, 30.0, false), 3.0));

  EXPECT_EQ(manager.current().state, LifecycleState::BcMpcFollow)
      << "v2.2 §13.2: takeover requested → BcMpcFollow, not KeepLast/DegradedHold";
}

// v2.2 §13.2: when NO BC-MPC take-over is signaled, three consecutive NLP
// failures must enter DegradedHold (fail-safe / SOTIF ISO 21448:2022) — NOT
// KeepLast stale corridor. This is the "no BC-MPC available" path; MRM-02
// escalation is wired upstream (M7 Slice K). Pins the existing v2.1 behavior as
// the v2.2 no-takeover contract.
TEST(CommittedRouteV22, DegradedHoldWhenNoBcMpc) {
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

  // No mark_bc_mpc_takeover() — BC-MPC not taking over.
  EXPECT_FALSE(manager.try_revise(
      candidate("fail-1", route_b_with_same_prefix(), 2U, 30.0, false), 1.0));
  EXPECT_FALSE(manager.try_revise(
      candidate("fail-2", route_b_with_same_prefix(), 2U, 30.0, false), 2.0));
  EXPECT_FALSE(manager.try_revise(
      candidate("fail-3", route_b_with_same_prefix(), 2U, 30.0, false), 3.0));

  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  EXPECT_EQ(manager.current().safety_concern_event,
            "nlp_consecutive_failures_ge_3_no_bcmpc")
      << "v2.2 §13.2: distinct event tag when BC-MPC is NOT taking over";
}

// v2.2 §13.2 regression guard: at consecutive_failures >= 3 the state must NEVER
// be KeepLast. v2.2 policy removes KeepLast-at-escalation entirely (SOTIF/IEC
// 61508 fail-safe/predictable). Only BcMpcFollow (takeover) or DegradedHold
// (no takeover) are permissible at the escalation threshold.
TEST(CommittedRouteV22, NoKeepLastStaleCorridorAtConsecutive3) {
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

  EXPECT_FALSE(manager.try_revise(
      candidate("fail-1", route_b_with_same_prefix(), 2U, 30.0, false), 1.0));
  EXPECT_FALSE(manager.try_revise(
      candidate("fail-2", route_b_with_same_prefix(), 2U, 30.0, false), 2.0));
  // Without takeover: at the 3rd failure state must be DegradedHold, never KeepLast.
  EXPECT_FALSE(manager.try_revise(
      candidate("fail-3", route_b_with_same_prefix(), 2U, 30.0, false), 3.0));
  EXPECT_NE(manager.current().state, LifecycleState::KeepLast);
  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);

  // With takeover: a fresh manager reaching 3 with takeover must be BcMpcFollow.
  CommittedAvoidanceRoute manager_bc;
  ASSERT_TRUE(manager_bc.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  manager_bc.mark_bc_mpc_takeover();
  EXPECT_FALSE(manager_bc.try_revise(
      candidate("fail-1", route_b_with_same_prefix(), 2U, 30.0, false), 1.0));
  EXPECT_FALSE(manager_bc.try_revise(
      candidate("fail-2", route_b_with_same_prefix(), 2U, 30.0, false), 2.0));
  EXPECT_FALSE(manager_bc.try_revise(
      candidate("fail-3", route_b_with_same_prefix(), 2U, 30.0, false), 3.0));
  EXPECT_NE(manager_bc.current().state, LifecycleState::KeepLast);
  EXPECT_EQ(manager_bc.current().state, LifecycleState::BcMpcFollow);
}

// v2.2 §13.2 (Codex integration blocker 2): once in BcMpcFollow, the committed
// route must STAY in BcMpcFollow — the stale/escalation gate
// (should_enter_degraded_hold) must NOT force it back to DegradedHold. This is
// the precondition for the publish path: MidMpcNode::publish_keep_last_ guards
// on state==BcMpcFollow to suppress republishing the stale NLP corridor. If the
// gate could transition it away, the guard would be ineffective. BC-MPC owns the
// maneuver via ReactiveOverrideCmd (架构 §L4); a stale NLP corridor must never be
// resurrected while BC-MPC is driving.
TEST(CommittedRouteV22, BcMpcFollowStableAcrossStaleGate) {
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  manager.mark_bc_mpc_takeover();
  EXPECT_FALSE(manager.try_revise(
      candidate("fail-1", route_b_with_same_prefix(), 2U, 30.0, false), 1.0));
  EXPECT_FALSE(manager.try_revise(
      candidate("fail-2", route_b_with_same_prefix(), 2U, 30.0, false), 2.0));
  EXPECT_FALSE(manager.try_revise(
      candidate("fail-3", route_b_with_same_prefix(), 2U, 30.0, false), 3.0));
  ASSERT_EQ(manager.current().state, LifecycleState::BcMpcFollow);

  // The stale gate must keep returning false past the 45s stale threshold while
  // BC-MPC owns the maneuver — DegradedHold would resurrect the stale corridor
  // via publish_keep_last_, violating §13.2.
  EXPECT_FALSE(manager.should_enter_degraded_hold(1000.0))
      << "v2.2 §13.2: BcMpcFollow must not be forced into DegradedHold by the stale gate";
  EXPECT_EQ(manager.current().state, LifecycleState::BcMpcFollow)
      << "state unchanged after stale gate probe";
}

// Phase 2.1/2.3 (R2/R6, spec v2.3 §3.2): the commit gate risk_trigger_event
// was rewritten to mirror tail-gate floor semantics. The legacy gate rejected
// any candidate when current range < cpa_hard, which on a rule14-ho approach
// is the steady-state geometry for the entire encounter — blocking the very
// CPA-opening maneuver it was supposed to author (optimized_committed_rejected
// × 790 in V2.3 phase 3b). The new gate:
//   - skips the floor when target is closing (active approach — maneuver IS
//     the CPA-opening action),
//   - only enforces the floor on the candidate's achieved terminal CPA when
//     the target is opening (release/recovery),
//   - rejects only when terminal_cpa < cpa_hard AND target opening.
// These tests pin each branch so a regression to "current range < hard → reject"
// is caught.

static CommittedRouteCandidate candidate_with_cpa(
    std::string plan_id,
    double current_cpa_m,
    double terminal_cpa_m,
    double cpa_hard_m,
    bool target_opening,
    bool nlp_ok = true) {
  CommittedRouteCandidate c;
  c.plan_id = std::move(plan_id);
  c.geometry = {GeoWP{34.001, 130.001, 5.0, "MID_MPC_OPTIMIZED"},
                GeoWP{34.002, 130.002, 5.0, "MID_MPC_OPTIMIZED"}};
  c.frozen_prefix_count = 0U;
  c.valid_until_s = 100.0;
  c.nlp_ok = nlp_ok;
  c.current_cpa_m = current_cpa_m;
  c.terminal_cpa_m = terminal_cpa_m;
  c.cpa_hard_m = cpa_hard_m;
  c.target_opening = target_opening;
  return c;
}

TEST(CommittedRouteRiskGate, ActiveApproachWithRangeBelowHardDoesNotReject) {
  // rule14-ho approach: range < 1852 (steady state), target closing.
  // The legacy gate rejected this and starved every optimized candidate.
  CommittedAvoidanceRoute manager;
  EXPECT_TRUE(manager.try_revise(
      candidate_with_cpa(
          "plan-active-approach",
          /*current_cpa_m=*/800.0,
          /*terminal_cpa_m=*/2000.0,  // candidate opens CPA past hard
          /*cpa_hard_m=*/1852.0,
          /*target_opening=*/false),
      0.0))
      << "active approach: maneuver IS the CPA-opening action, must not be rejected";
}

TEST(CommittedRouteRiskGate, ReleaseWithTerminalCpaAboveHardAccepts) {
  // Target opening, candidate achieved safe terminal CPA. Should accept.
  CommittedAvoidanceRoute manager;
  EXPECT_TRUE(manager.try_revise(
      candidate_with_cpa(
          "plan-release-safe",
          /*current_cpa_m=*/800.0,
          /*terminal_cpa_m=*/2200.0,
          /*cpa_hard_m=*/1852.0,
          /*target_opening=*/true),
      0.0))
      << "release with safe terminal CPA: must accept";
}

TEST(CommittedRouteRiskGate, ReleaseWithTerminalCpaBelowHardRejects) {
  // Target opening but candidate did NOT open enough — true fail-safe case.
  CommittedAvoidanceRoute manager;
  EXPECT_FALSE(manager.try_revise(
      candidate_with_cpa(
          "plan-release-unsafe",
          /*current_cpa_m=*/800.0,
          /*terminal_cpa_m=*/1200.0,  // below hard
          /*cpa_hard_m=*/1852.0,
          /*target_opening=*/true),
      0.0))
      << "release with terminal CPA still below hard floor: must reject";
  EXPECT_EQ(manager.current().safety_concern_event,
            "terminal_cpa_below_hard_floor_on_release");
}

TEST(CommittedRouteRiskGate, RangeAboveHardNeverRejects) {
  // Far target — no floor concerns regardless of phase.
  CommittedAvoidanceRoute manager;
  EXPECT_TRUE(manager.try_revise(
      candidate_with_cpa(
          "plan-far",
          /*current_cpa_m=*/5000.0,
          /*terminal_cpa_m=*/0.0,
          /*cpa_hard_m=*/1852.0,
          /*target_opening=*/false),
      0.0));
}

TEST(CommittedRouteRiskGate, ActiveApproachEvenWithSmallTerminalCpaAccepts) {
  // Active approach, terminal CPA small (maneuver hasn't fully developed yet).
  // Must still accept — the maneuver is what opens CPA, requiring it already
  // safe is the very bug tail-gate also fixed (types.hpp:824-842).
  CommittedAvoidanceRoute manager;
  EXPECT_TRUE(manager.try_revise(
      candidate_with_cpa(
          "plan-active-small-terminal",
          /*current_cpa_m=*/600.0,
          /*terminal_cpa_m=*/600.0,
          /*cpa_hard_m=*/1852.0,
          /*target_opening=*/false),
      0.0))
      << "active approach with small terminal CPA: must accept (mirrors tail-gate)";
}

// Phase 1.4 (G-M5-2, spec v2.3 §15): lifecycle_state_name is the stable name
// used in ASDR decision_json so audit consumers can group by lifecycle state
// without depending on the numeric enum value (which changes when states are
// inserted). Pin every state's name so a rename or insertion breaks tests
// before it breaks downstream audit parsers.
TEST(LifecycleStateName, AllStatesHaveStableNames) {
  EXPECT_STREQ(lifecycle_state_name(LifecycleState::Idle), "Idle");
  EXPECT_STREQ(lifecycle_state_name(LifecycleState::CandidateEvaluating), "CandidateEvaluating");
  EXPECT_STREQ(lifecycle_state_name(LifecycleState::Committed), "Committed");
  EXPECT_STREQ(lifecycle_state_name(LifecycleState::HeartbeatOnly), "HeartbeatOnly");
  EXPECT_STREQ(lifecycle_state_name(LifecycleState::KeepLast), "KeepLast");
  EXPECT_STREQ(lifecycle_state_name(LifecycleState::Stale), "Stale");
  EXPECT_STREQ(lifecycle_state_name(LifecycleState::DegradedHold), "DegradedHold");
  EXPECT_STREQ(lifecycle_state_name(LifecycleState::Released), "Released");
  EXPECT_STREQ(lifecycle_state_name(LifecycleState::BcMpcFollow), "BcMpcFollow");
}

TEST(LifecycleStateName, DistinctAcrossAllStates) {
  // No two states may share a name — otherwise ASDR audit consumers cannot
  // disambiguate them in decision_json.
  const std::vector<LifecycleState> states = {
      LifecycleState::Idle,
      LifecycleState::CandidateEvaluating,
      LifecycleState::Committed,
      LifecycleState::HeartbeatOnly,
      LifecycleState::KeepLast,
      LifecycleState::Stale,
      LifecycleState::DegradedHold,
      LifecycleState::Released,
      LifecycleState::BcMpcFollow,
  };
  std::vector<std::string> names;
  names.reserve(states.size());
  for (const auto s : states) {
    names.emplace_back(lifecycle_state_name(s));
  }
  std::sort(names.begin(), names.end());
  const auto last = std::unique(names.begin(), names.end());
  EXPECT_EQ(last, names.end())
      << "lifecycle_state_name returned duplicate names; audit consumers cannot disambiguate";
}
