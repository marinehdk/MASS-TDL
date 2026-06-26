# M5 Mid-MPC J_colreg Cost Formula Fix — colreg-rule14-ho

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the M5 Mid-MPC planner converge to a real rule-14 starboard turn (instead of a straight-heading NORMAL plan that drives no rudder) in the `colreg-rule14-ho` scenario on the A4000 acceptance gate. Verified by the existing `scripts/a4000-acceptance.sh` going from `ACCEPTANCE FAIL` (A_turn net=0°) to `ACCEPTANCE PASS` (A_turn net > 20°).

**Architecture:** Single-line change in the M5 COLREGs cost term (file: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp:143`). The current formula is the well-known **instantaneous-evaluation pitfall** (Johansen 2016 family): the penalty `fmax(0, cpa² - d²)` vanishes when the target is *outside* `cpa_safe`, leaving the MPC with `cost_colreg = 0` across the entire horizon. Because the route bearing is 0° and the own-ship heading is 0°, `cost_dist = 0` too, and the M4 heading-bound `psi ≥ 16°` is the only active constraint — but IPOPT is happy to relax it for a costless solution and the bridge arming gate then publishes a 0° plan that drives no rudder. Geometric fallback fires only after 6 consecutive MPC failures, hence the intermittent ship-turning behavior seen in earlier runs.

The fix uses the standard remedy from the MPC COLREGs literature (Johansen 2016, Eriksen 2019, Tam 2010): **exponential barrier on the safety margin** `μ·exp(-ζ·(d_safe - d))`. This is C∞, has gradient everywhere, is singularity-free at d=0, and produces no perverse incentive (no multiplicative `cpa²` factor that the MPC could zero trivially).

**Tech Stack:** C++17, CasADi 3.7.2 (with IPOPT plugin, already built into the sil-nodes image during the 2026-06-03 Dockerfile fix), ROS2 Humble, colcon, Docker 29.1.3, Ubuntu 22.04.

**Evidence trail (Phase-1 investigation, A4000, 2026-06-03):**
- M4 publishes `behavior=1 AVOIDANCE` with `heading_min_deg=16` correctly. (No bug in M4.)
- M5 publishes NORMAL plans with 4 waypoints along heading ≈ 0° and `cost_colreg=0 cost_dist=0 cost_vel=0` (echoed via `ros2 topic echo`).
- Geometric fallback (DEGRADED plans, `turn_radius=50m target_psi=35°`) is the *only* path that produces a real turn — confirmed by correlating bridge latch logs.
- NLM verification: `nlm ask --notebook colav_algorithms` 🟢 high-confidence response on (a) the current formula is the documented "instantaneous-evaluation pitfall" and (b) exponential-on-safety-margin is the recommended remedy in the MPC COLREGs literature.

---

## ⚠️ Guardrails (read before every task)

- **NEVER** change `assemble_input_` x/y/cos correction (L126-128) or the `normalize_angle` function in `mid_mpc_node.cpp`. Those are not the bug; touching them risks introducing a new coordinate-frame error.
- **DO NOT** widen the `TURN_NET_MIN_DEG` threshold in `web/e2e/mvp_consistency.spec.ts` to "make the test pass". The threshold is now ground-truthed against `safety=1.0/compliance=1.0` runs and must not be loosened.
- **DO NOT** disable the geometric fallback or change the bridge arming logic. Both are correct; they remain as a safety net.
- The 2026-06-03 Dockerfile casadi+ipopt plugin fix is **prerequisite** and already landed; this plan assumes `sil-nodes` can build and run m5_mid_mpc_node. Verify with `ros2 node list` before starting.
- No shared-host / jitsi / fat-system guardrails apply here (this plan only rebuilds `mass-l3-sil-sil-nodes` and re-runs our acceptance script; ports unchanged).

---

## Task 0: Sanity check (A4000)

**Files:** none.
**Steps:**
- [ ] `ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh && ./scripts/a4000-acceptance.sh'` — confirm the *baseline* failure mode (A_turn net=0°, A_rtf≈10.00 in band). If it passes today, the bug has drifted and this plan is no longer needed; stop and re-investigate.
- [ ] `ssh a4000 'C=$(docker compose -f docker-compose.yml -f docker-compose.a4000.yml ps -q sil-nodes); docker exec $C bash -lc "source /opt/ros/humble/setup.bash; source /opt/ws/install/setup.bash 2>/dev/null; ros2 node list 2>/dev/null | grep m5_mid_mpc_node"'` — M5 alive.
- [ ] `ssh a4000 'docker exec $(docker compose -f docker-compose.yml -f docker-compose.a4000.yml ps -q sil-nodes) bash -lc "ls /usr/lib/x86_64-linux-gnu/libcasadi_nlpsol_ipopt.so* 2>/dev/null || ls /usr/local/lib/libcasadi_nlpsol_ipopt.so* 2>/dev/null"'` — ipopt plugin present.
- [ ] `git status` clean on `fix/m5-casadi-ipopt` branch. If not, resolve drift first.

**Accept:** A_turn is RED, M5 node present, ipopt plugin present, branch clean.

---

## Task 1: Apply the 1-line J_colreg change

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp`

**Steps:**
- [ ] In the anonymous namespace at the top of the file (L20-47), add `constexpr double kZeta = 1.0e-3;` next to the existing `kIpoptMaxIter`/`kIpoptTol`/`kIpoptMaxCpuTime` block.
- [ ] At L143 (inside `build_colreg_cost_()`), replace:
  ```cpp
  cost = cost + tw * casadi::MX::fmax(zero, cpa2 - d2);
  ```
  with:
  ```cpp
  const casadi::MX d_safe = casadi::MX::sqrt(d2 + 1.0);   // [m] +1 keeps sqrt differentiable at d=0
  cost = cost + tw * casadi::MX::exp(-kZeta * (cpa - d_safe));
  ```
  where `cpa = slot(p_, kIdxCpaSafe)` (the existing L109 variable, which is the square root of cpa² → use it directly).
- [ ] No other changes in the file. The geometric fallback path, the `waypoint_generator`, and the bridge are untouched.
- [ ] `cd /Users/marine/Code/MASS-L3-Tactical\ Layer && git diff src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp` — diff is exactly: 1 new constant + 1 replaced cost line (2 effective hunk).

**Accept:** Diff matches the above exactly; no incidental changes.

---

## Task 2: Rebuild sil-nodes image

**Files:** none (uses existing Dockerfile; no Dockerfile changes this task).
**Steps:**
- [ ] `scp src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp a4000:~/Code/mass-l3/src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp`
- [ ] `ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh && docker compose build sil-nodes 2>&1 | tail -20'` — confirm rebuild succeeds. Expect ≤5 min thanks to ccache shared mount + colcon build cache.
- [ ] `ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh && docker compose up -d --force-recreate sil-nodes'`
- [ ] `ssh a4000 'C=$(docker compose -f docker-compose.yml -f docker-compose.a4000.yml ps -q sil-nodes); for i in 1 2 3 4 5 6; do docker exec $C bash -lc "source /opt/ros/humble/setup.bash; source /opt/ws/install/setup.bash 2>/dev/null; ros2 node list 2>/dev/null | grep -q m5_mid_mpc_node && echo M5-up && break"; sleep 5; done'` — M5 back up.
- [ ] `ssh a4000 'C=$(docker compose -f docker-compose.yml -f docker-compose.a4000.yml ps -q sil-nodes); docker exec $C bash -lc "source /opt/ros/humble/setup.bash; source /opt/ws/install/setup.bash 2>/dev/null; ros2 topic hz /l3/m5/avoidance_plan 2>/dev/null"' — M5 is publishing at ~1 Hz.

**Accept:** Rebuild succeeded, M5 node back in `ros2 node list`, `/l3/m5/avoidance_plan` publishing ≥0.5 Hz.

---

## Task 3: Run acceptance gate

**Files:** none.
**Steps:**
- [ ] `ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh && ./scripts/a4000-acceptance.sh'`
- [ ] Inspect exit code and tail of output. Required:
  - `[1] orchestrator health` PASS
  - `[2] headless RTF sweep {1,5,10}x` all PASS (efficiency ≥85%)
  - `[3] full multi-screen Playwright @10x` exit code 0, with `A_turn` line showing `net > 20°` and `A_recon` paired matches ≥30 with median |Δ| < 10°

**Accept:** Exit code 0, A_turn net > 20° printed in the Playwright log.

---

## Task 4: Negative-outcome handling

**If A_turn still 0° or < 20°** (worst case, ~1% probability given the literature backing):
- [ ] Dump M5 cost terms in a fresh run to confirm the new formula is being evaluated:
  `ssh a4000 'C=$(docker compose -f docker-compose.yml -f docker-compose.a4000.yml ps -q sil-nodes); docker exec $C bash -lc "source /opt/ros/humble/setup.bash; source /opt/ws/install/setup.bash 2>/dev/null; ros2 topic echo --once /l3/m5/avoidance_plan 2>/dev/null | grep rationale'`
  - Should print `MPC converged in N ms; ipopt_iter=...; cost_colreg=... cost_dist=... cost_vel=...` with `cost_colreg > 0`.
- [ ] If `cost_colreg > 0` but the ship still doesn't turn → check the bridge logs for LATCH/arming:
  `ssh a4000 'docker compose logs --tail=2000 sil-nodes 2>&1 | grep -E "LATCHED|AVOIDANCE ARM|BRIDGE-AVOID" | tail -30'`
  - If LATCHED is firing with a non-zero target → bridge is fine; the issue is downstream (HeadingController tuning, rudder physics). Open a follow-up investigation; do not loosen the A_turn threshold.
  - If LATCHED is *not* firing despite valid M5 plans → re-check `LATCH_MIN_HOLD_S` and the M3 cold-start guard in `_on_avoidance_plan`.
- [ ] If `cost_colreg = 0` still → the new formula was not loaded. Verify with `docker exec ... md5sum /opt/ws/install/m5_tactical_planner/lib/.../mid_mpc_nlp_formulation*.so` against the local source md5.

**If IPOPT timeouts become frequent** (`solver_status=1 Timeout` in sil-nodes logs):
- [ ] Increase `kIpoptMaxCpuTime` from `0.45` to `0.9` (file: `mid_mpc_nlp_formulation.cpp` L31). Do not increase `kIpoptMaxIter` (the formulation is small; timeout is the bottleneck).
- [ ] Rebuild + re-run Task 2-3.

**Do NOT** under any circumstance:
- Loosen `TURN_NET_MIN_DEG` in the spec file.
- Switch off the geometric fallback as a "test fix".
- Touch the bridge arming logic or the M4 heading bounds.

---

## Task 5: Commit + push + sync

**Files:** the file modified in Task 1, only.
**Steps:**
- [ ] `cd /Users/marine/Code/MASS-L3-Tactical\ Layer`
- [ ] `git checkout -b fix/m5-colreg-cost-formula 2>/dev/null || git checkout fix/m5-colreg-cost-formula`
- [ ] `git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp`
- [ ] `git commit -m "fix(m5): J_colreg exponential barrier — no more cost=0 plateau (rule14 intermittent)

  The old fmax(0, cpa^2 - d^2) cost vanishes when d > cpa_safe (the well-
  known instantaneous-evaluation pitfall in Johansen-2016 family), so
  during the long pre-CPA approach in colreg-rule14-ho the MPC sees
  cost_colreg = 0 across the entire horizon. With cost_dist = 0 (own
  heading already equals planned bearing = 0) the solver has no
  incentive to turn; the M4 heading bound (psi >= 16 deg) is the only
  active constraint and IPOPT is happy to relax it for a costless
  solution. The bridge then arms on a 0 deg plan whose waypoints drive
  atan2(46, inf) = 0 rudder, so the ship never turns.

  Replace with the standard MPC COLREGs literature remedy: an
  exponential barrier on the safety margin
  cost = mu * exp(-zeta * (cpa - d)).  This is C^inf, singularity-free
  at d=0, has gradient everywhere (the MPC feels the obstacle far
  before CPA), and avoids the perverse incentive of multiplying by
  cpa^2 (which the MPC could zero trivially by reducing cpa).

  NLM grounding: nlm-ask --notebook colav_algorithms (high confidence)
  confirms the current formula as the documented pitfall and the
  exponential-on-safety-margin form as the recommended remedy
  (Johansen 2016, Eriksen 2019, Tam 2010).

  Verified on A4000: scripts/a4000-acceptance.sh now passes the
  full multi-screen Playwright gate (A_turn net > 20 deg, A_recon
  paired matches >= 30) instead of failing with A_turn net = 0 deg.
  Geometric fallback and bridge arming logic untouched."`
- [ ] `git checkout main && git merge --ff-only fix/m5-colreg-cost-formula`
- [ ] `git push origin main && git push gitlab main:l3-tdl`
- [ ] `ssh a4000 'cd ~/Code/mass-l3 && git pull --ff-only origin l3-tdl 2>&1 | tail -2'` — server in sync.

**Accept:** Two new commits visible on `origin/main` and `gitlab/l3-tdl`, server HEAD matches local.

---

## Verification (end-to-end)

The single source of truth is `scripts/a4000-acceptance.sh`. Run after every Task 1/2 change:
```bash
ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh && ./scripts/a4000-acceptance.sh'
```
Required final state: `ACCEPTANCE PASS` with A_turn net > 20° in the Playwright log line.

The detailed per-assertion numbers (A_rtf, A_turn, A_recon) are also written to `runs/mvp_consistency/10x_*/metrics.json` on the server for forensic review.
