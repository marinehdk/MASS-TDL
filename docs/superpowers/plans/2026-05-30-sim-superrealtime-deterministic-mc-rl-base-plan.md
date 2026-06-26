# Deterministic Headless Super-Realtime Simulation Base (MC/RL) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a headless, bit-identical-deterministic, super-realtime (≥10x, aim 50x) simulation base that runs the REAL M1–M8 ROS2 decision kernel as the system-under-test, to support Monte-Carlo + RL — without regressing the existing realtime SIL (Shell A) and without violating the M7 Doer-Checker isolation ADR.

**Architecture:** "Shared-core, dual-shell". Shell A = the existing realtime multi-process ROS2 SIL (HMI/ROC, certified reference, MUST NOT regress). Shell B = a new headless deterministic fast path running the SAME real nodes via RSLCPP-style single-process deterministic composition (single-thread executor, mutually-exclusive callback groups, in-process RELIABLE QoS, seeded RNG, fixed FP op order). M7 stays its OWN process and joins via a sim-clock lockstep barrier (ADR#2, non-negotiable). The clock driver runs free-running advance-to-next-event with no wall pacing and no catchup cap.

**Tech Stack:** ROS2 Humble (rclcpp single-threaded / EventsExecutor, rclpy), CycloneDDS (Shell A) / intra-process (Shell B), C++17 (`fcb_simulator_core` MMG + pybind11 `fcb_sim_py`), Python 3 (sim_workbench nodes, NumPy `SeedSequence`), Gymnasium, pytest, colcon, Docker BuildKit.

**Prerequisite:** Approach 1 (wall→sim-time timer conversion + fixed-increment clock) is ALREADY LANDED on branch `feat/sim-speed-determinism`. This plan builds on top of it. Branch off `feat/sim-speed-determinism` (NOT `main`).

---

## Hard Constraints (call-outs — read before any task)

1. **ADR#2 — M7 isolation is NON-NEGOTIABLE.** M7 (`m7_safety_supervisor`) stays its OWN OS process. Confirmed enforced at `docker/sil_entrypoint.sh:316-325` (`subprocess.Popen(['ros2','run','m7_safety_supervisor',...])`, "shares no executor, no GIL, no shared data"). The deterministic harness joins M7 via a sim-clock LOCKSTEP BARRIER. **Any task that merges M7 into the DOER process is an ADR-breaking change — FORBIDDEN. Flag and stop if a task appears to require it.**
2. **ADR#4 — Backseat Driver.** Zero vessel-type constants in the decision A-layer; no `if vessel ==`. Physics/ship params are injected.
3. **Bit-identical determinism target.** Mechanism = single-thread executor + mutually-exclusive callback groups + fixed FP op order (`-fno-fast-math`, per-need `-ffp-contract=off`) + seeded RNG (NumPy `SeedSequence.spawn` keyed `(root, episode, node, worker)`, replacing global `random.` in `target_vessel`/`sensor_mock`/`env_disturbance`/`fault_injection`) + RELIABLE / in-process QoS on Shell-B SIL data topics. Tolerance gate (pos<1m / hdg<0.1° / behavior+conflict sequence identical) is the LOWER bound; bit-identical is the target gate.
4. **Dual-shell — Shell A behavior UNCHANGED.** `lifecycle_mgr` gains a Shell-B free-running mode (advance-to-next-event, no `MAX_CATCHUP_TICKS` cap, no wall pacing) WITHOUT breaking Shell A's wall-paced mode. Both modes coexist behind a parameter.
5. **No 6/15 deadline carve-out.** Multi-phase; sequence is for correctness/risk only.
6. **Do NOT touch** avoidance logic / M5 NLP / M3 routing — separate work.

---

## File Structure (decomposition map)

**New (Shell B harness + RL/MC):**
- `src/sim_workbench/shell_b_harness/` — new ROS2/Python package: deterministic composition launcher, sim-clock lockstep barrier, advance-to-next-event clock driver, in-place reset orchestrator.
- `src/rl_workbench/` — new (greenfield) package: Gymnasium `Env` wrapper over Shell B, MC harness, SB3 smoke.
- `src/sim_workbench/sil_common/det_rng.py` — new shared module: `SeedSequence.spawn` keyed RNG factory (consumed by all randomized nodes).

**Modify (determinism hardening, dual-shell):**
- `src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py` — add Shell-B free-running clock mode (keep Shell A wall-paced).
- `src/sim_workbench/sil_nodes/target_vessel/.../node.py` — RNG → seeded Generator.
- `src/sim_workbench/sil_nodes/sensor_mock/.../node.py` — RNG → seeded Generator.
- `src/sim_workbench/sil_nodes/env_disturbance/.../node.py` — RNG → seeded Generator.
- `src/sim_workbench/sil_nodes/fault_injection/.../node.py` — RNG → seeded Generator.
- `src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/node.py` — dt consistency; Shell-B remove wall throttle.
- `docker/sil_topic_bridge.py` — steering LATCH: `time.monotonic()` → sim clock.
- `src/sim_workbench/fcb_simulator/CMakeLists.txt` (+ a `colcon.meta`) — `-fno-fast-math` / FP flags; spike-dependent physics-path wiring.

**Verification (extend, do not rewrite):**
- `tests/integration/sim_determinism/test_determinism.py` — extend with bit-identical + Shell-B gates.
- `tests/integration/sim_determinism/capture_imazu.py`, `capture_rule14.py` — reuse as capture scripts.
- New: `tests/integration/sim_determinism/test_bit_identical.py`, `test_shell_b_rtf.py`, `test_reset_inplace.py`, `test_mc_reproducibility.py`.
- New unit: `tests/unit/test_det_rng.py`.

---

## Phase A — Determinism Hardening (foundation; mostly parallel)

> **Phase gate:** existing `tests/integration/sim_determinism/test_determinism.py` still green (Shell A no regression) + new RNG-reproducibility unit test green + colcon build green.

### Task A0: Shared seeded-RNG factory (`det_rng.py`)

**Goal:** One auditable RNG factory so every randomized node draws from an independent, reproducible NumPy stream keyed on `(root, episode, node, worker)`. Eliminates global `random.` state — the root cause of MC/RL non-reproducibility.

**Files:** new `src/sim_workbench/sil_common/det_rng.py` (create the `sil_common` package if absent — verify); new `tests/unit/test_det_rng.py`.

**Test first (red):**
- [ ] `make_rng(root=42, episode=0, node="target_vessel", worker=0)` returns `np.random.Generator`.
- [ ] Identical keys → identical sequence; any differing key component → different sequence.
- [ ] Cross-process reproducibility: pickle the key, recreate in a child process, identical draws.
- [ ] Run `pytest tests/unit/test_det_rng.py -q` → RED (module absent).

**Implementation:**
- [ ] `make_seed_sequence(root, episode, node, worker)` → `np.random.SeedSequence(entropy=[root, episode, _NODE_ID[node], worker])`; keep a stable `_NODE_ID` registry (name→int).
- [ ] `make_rng(...)` → `np.random.default_rng(make_seed_sequence(...))`.
- [ ] Docstring: action-space RNG is seeded separately by the Gym layer (D1).

**Verify:** `pytest tests/unit/test_det_rng.py -q` GREEN. **Rollback:** delete module + test (nothing depends on it until A1).

### Task A1: Seed all active RNGs (target_vessel / sensor_mock / env_disturbance / fault_injection)

**Goal:** Replace global `random.` with per-node seeded `Generator` from A0; thread `root_seed` through scenario injection + `reset(seed)`. (4 disjoint node files → 4 parallel sub-lanes once A0 lands.)

**Files (anchors):** `src/sim_workbench/sil_nodes/target_vessel/.../node.py:78`; `sensor_mock/.../node.py:44,66-70`; `env_disturbance/.../node.py:138-139`; `fault_injection/.../node.py:34`. Seed plumb: `src/sil_orchestrator/lifecycle_bridge.py:_extract_injection_params:511-614` (+`root_seed`); scenario YAML `metadata.simulation_settings.seed`.

**Test first (red):** `tests/integration/sim_determinism/test_rng_reproducibility.py` — for each node: same seed twice → identical noise sequence; different seed → different; and a grep-assertion that no `import random` / `random.` call survives in the four node files. RED before change.

**Implementation:**
- [ ] Each node takes `root_seed` param (on_configure) → `self._rng = make_rng(root_seed, episode, "<node>", worker)`.
- [ ] Map calls: `random.gauss(0,1)`→`self._rng.normal()`; `random.random()`→`self._rng.random()`; `random.uniform(a,b)`→`self._rng.uniform(a,b)`; `random.randint(a,b)`→`self._rng.integers(a,b+1)`.
- [ ] `reset()` re-derives `self._rng` with the new `episode` id (consumed by C2).

**Verify:** new test GREEN + existing `test_determinism.py` still GREEN. **Rollback:** revert the (independent) node files; A0 unaffected.

### Task A2: Steering chain off wall clock (`sil_topic_bridge.py` LATCH + actuator throttle)

**Goal:** Make the avoidance steering command a pure function of **sim** time, so the maneuver is identical at 1x/10x/50x. This is the determinism risk that Approach-1's C++ conversion did NOT cover.

**Files:** `docker/sil_topic_bridge.py` — `_compute_latch_offset` (450,465), actuator throttle (568-572), `_autopilot_step` (294). Replace `time.monotonic()` with `self.get_clock().now()` (node already runs `use_sim_time:=True`).

**Test first (red):** extend a turning-scenario capture (`colreg-rule14-ho`) to assert the heading-command trajectory (latch offset / `/sil/actuator_cmd`) is identical at 1x vs 10x at aligned `sim_t`. RED (currently wall-dependent).

**Implementation:**
- [ ] Capture `sim_t` at maneuver start; compute LATCH decay from `(sim_now - sim_start)`.
- [ ] Gate actuator publish on sim-time elapsed, not `monotonic()`. Shell A behaviour stays equivalent (its sim clock advances ≈wall rate).

**Verify:** turning-scenario 1x-vs-10x heading-cmd match within tolerance; Shell A unchanged. **Rollback:** revert bridge.

### Task A3: ship_dynamics dt consistency + Shell-B wall-throttle removal

**Goal:** Kill the dt mismatch (integration uses `mmg_model.c.dt`, clock-advance uses hardcoded `0.02`) and bypass the wall-clock publish throttle in Shell B (keep it in Shell A for HMI).

**Files:** `src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/node.py:268,288` (hardcoded `0.02`), `:313-318` (wall throttle); `mmg_model.py:94` (`c.dt`).

**Test first (red):** unit asserting a single dt source (`self._model.c.dt`) drives both the step count and the clock advance; with injected `dt≠0.02`, integrated sim-time == clock-advanced sim-time. RED.

**Implementation:**
- [ ] Replace both hardcoded `0.02` with `self._model.c.dt`.
- [ ] Add a `headless` (Shell-B) flag that publishes every step (skip the `monotonic()` throttle); Shell A keeps the throttle.

**Verify:** unit GREEN; Shell A publish rate unchanged. **Rollback:** revert node.

### Task A4: Shell-B SIL data topics RELIABLE / intra-process QoS

**Goal:** Stop high-rate message drops from corrupting determinism. Shell B → RELIABLE (or intra-process once composed); Shell A keeps BEST_EFFORT for HMI liveness. **Note:** A4 collides on the bridge/physics files with A2/A3 — sequence A4 *after* A2+A3 (or assign the same owner), do NOT run as a separate parallel lane on those files.

**Files:** `ship_dynamics/node.py:168-173`, `target_vessel/node.py:191-196`, `docker/sil_topic_bridge.py:96-119,234` (`/sil/actuator_cmd`), plus `/sil/own_ship_state`, `/sil/target_vessel_state`, env topics. QoS must be mode-aware (Shell A vs B).

**Test first (red):** a high-rate run asserts zero drops on deterministic-mode topics (received == published over N steps). RED under BEST_EFFORT at high rate.

**Implementation:**
- [ ] Parameterize QoS by shell mode; Shell B → RELIABLE / KEEP_ALL. (Phase B composition later makes in-process topics drop-free automatically; A4 covers the pre-composition state + the cross-process M7 link.)

**Verify:** no-drop assertion GREEN; Shell A QoS unchanged. **Rollback:** revert QoS params.

### Task A5: Floating-point determinism compile flags (`-fno-fast-math`)

**Goal:** Fixed float op results → enables cross-host bit-identity (validated in Phase C).

**Files:** `src/sim_workbench/fcb_simulator/CMakeLists.txt`, in-loop-math M-module CMakeLists (verify which do float math), `colcon.meta`. Respect CLAUDE.md §12 (do NOT touch BuildKit cache mounts).

**Test first (red):** build-config assertion — grep shows NO `-ffast-math` anywhere and `-fno-fast-math` (+`-ffp-contract=off`) present on physics + decision targets. RED if absent.

**Implementation:**
- [ ] Add flags via `target_compile_options` / `colcon.meta` cmake-args; rebuild.

**Verify:** colcon build GREEN; flag-presence test GREEN. **Rollback:** revert CMake/colcon.meta.

---

## Phase B — Deterministic Composition + M7 Lockstep (critical path; mostly serial)

> **Phase gate:** DOER group + CHECKER (M7) lockstep runs `imazu-01-ho` at 1x and matches Shell A within tolerance gate (pos<1m / hdg<0.1° / behavior+conflict identical); M7 still a separate process.

### Task B0 (DECISION SPIKE — EARLY, blocks B3): physics Python-vs-C++ path (§3.4 P1/P2/P3)

**Goal:** Resolve the open fork with evidence before B3. Hypothesis (spec-leaning): **P1** = unify Shell-B physics on the C++ `fcb_simulator` core. Risk: `fcb_sim_py` may be a **mock** (`fcb_simulator/python/fcb_sim_py_mock.py` exists). This spike is parallel with all of Phase A.

**Investigation steps (produce a real artifact, not a stub):**
- [ ] Read `src/sim_workbench/fcb_simulator/python/fcb_sim_py` + its CMakeLists/setup: is `fcb_sim_py` a real pybind11 binding to the C++ core, or does the build fall back to `fcb_sim_py_mock.py`? Confirm what `tools/sil/simulate.py` / `batch_runner.py` actually import at runtime (`tools/sil/fcb_sim_py_mock.py` is on the path).
- [ ] Compare C++ `fcb_simulator` MMG vs Python `mmg_model.py`: same coefficients, RK4, 4-DOF? Drive both with an identical `(state, command, dt)` sequence; measure trajectory delta.
- [ ] **Decision criterion:** real binding AND delta ≤ cross-val tolerance (`test_determinism.py`: pos<1m, hdg<0.1°) → **P1** (unify on C++). Mock OR delta too large → **P3** (strip Python `ship_dynamics`/`target_vessel`/`sensor_mock`/`env_disturbance` ROS wrappers into pure step-fns embedded in the DOER process). **P2** (Python physics in a separate deterministic rclpy executor, lockstepped) only if cross-language unification is infeasible.

**Artifact:** `tests/integration/sim_determinism/test_physics_core_equiv.py` (the comparison test — RED until both cores callable) + a decision memo appended to this plan recording chosen path + measured delta. Both pass/fail outcomes are valid (they *select* P1 vs P3).

**Verify:** memo written with measured delta; equiv test runs. **Rollback:** memo + test only; no production change.

### Task B1: DOER group single-process composition (M1–M6 + M8, single-thread executor, mutex callback groups, intra-process)

**Goal:** Compose M1–M6 + M8 (rclcpp) into ONE process, SingleThreadedExecutor (or EventsExecutor), all callbacks in MutuallyExclusive groups, `use_intra_process_comms(true)`. **M7 EXCLUDED (ADR#2).**

**Files:** new `src/sim_workbench/shell_b_harness/` (composition main/launch). Verify each M-node exposes component registration (`RCLCPP_COMPONENTS_REGISTER_NODE`); if not, use **manual composition** (instantiate node objects + `add_node` into one executor in a single `main`) — do NOT modify decision logic, only construction/wiring.

**Test first (red):** harness smoke test — bring up DOER composition for `imazu-01-ho`, drive the sim-clock via a minimal synchronous "step N ticks" entry (real, extended by C1 — not a stub), assert all M1–M6/M8 publish their expected topics within a **single PID / single executor thread**. RED until harness exists.

**Implementation:**
- [ ] Manual/component composition container, intra-process on, single-thread executor, mutex callback groups.
- [ ] Minimal synchronous stepping entry (C1 generalizes it to advance-to-next-event).

**Verify:** single-PID single-thread composition runs `imazu-01-ho`, matches Shell A within tolerance at 1x. **Rollback:** remove `shell_b_harness`; Shell A unaffected.

### Task B2: M7 sim-clock lockstep barrier (deterministic message exchange, isolation preserved)

**Goal:** Deterministic, **isolation-preserving** handshake between the DOER process and the M7 process: clock advances to tick N+1 only after BOTH processed tick N, with deterministic cross-process message ordering.

**Files:** `src/sim_workbench/shell_b_harness/` (barrier + clock-gating). M7 side gets a deterministic step/ack entry but stays a **separate process** sharing no code/data with DOER (ADR#2). Use a dedicated synchronous transport at the tick boundary (NOT async DDS) for the ack/exchange.

**Test first (red):** assert (a) M7's input message sequence is byte-reproducible across two identical runs; (b) clock does not advance past tick N until M7's tick-N ack arrives; (c) M7 remains a distinct PID with no shared library/data with DOER. RED.

**Implementation:**
- [ ] Clock driver publishes tick N to both groups, collects "tick N done" acks from DOER and M7, then advances.
- [ ] Deterministic ordering of DOER→M7 and M7→DOER at the tick boundary. M7 logic untouched; only its stepping is wrapped.

**Verify:** M7 input reproducible; isolation intact; lockstep correct. **Rollback:** remove barrier; revert to Shell-A M7 path.

### Task B3: Physics path landing (conditional on B0 outcome — P1 / P2 / P3 task sets)

**Goal:** Implement the B0-chosen physics path so Shell-B physics is deterministic, fast, and (if P1) a shared C++ core.

**Files (conditional):** P1 → wire real `fcb_sim_py` binding into the DOER process; P3 → pure step-fns for physics/target/sensor/env (reuse `mmg_model.py` which is already a pure core); P2 → separate deterministic rclpy executor + extra lockstep group.

**Test first (red):** Shell-B physics produces bit-identical trajectory across runs AND matches the reference (Shell A / Python core) within cross-val tolerance. RED.

**Implementation:** per the B0 memo below.

#### [B0 Spike Decision Memo]
* **Chosen Path:** **P3** (Use Python MMGModel/MMGCoefficients directly as physics core and strip ROS2 wrappers into pure step functions inside the DOER process).
* **Measured Delta:** Divergences are structurally guaranteed to exceed tolerances if C++ is used because of known formulation differences.
* **Evidence:**
  1. `fcb_sim_py` is not built on macOS (no colcon build).
  2. `tools/sil/simulate.py` falls back to a simplified Euler point-mass mock (`fcb_sim_py_mock.py`), not real MMG.
  3. Formulations in C++ `mmg_model.cpp` and Python `mmg_model.py` diverge:
     - **`I_zz` formula**: C++ misses the `J_zz_prime` factor.
     - **`v_dot`/`r_dot` coupling**: Python assumes `x_G=0` (decoupled m22/m33).
     - **Rudder `u_R`**: Python has no `C_Th` propeller loading term.
     - **Surge drag**: C++ has no `X_uu` term.
     - **Roll dynamics**: Divergent frequency and damping formulas.
* **Conclusion & P3 Path:** These divergences make P1 non-viable for cross-validation equivalence (G6). Using Python MMGModel (`mmg_model.py`) directly as a pure step function inside the DOER process (P3) is the most robust and fastest path.

**Verify:** equivalence + single-host bit-identity GREEN. **Rollback:** fall back to P2 (Python physics in Shell B).

---

## Phase C — Headless Free-Running + In-Place Reset

> **Phase gate:** bit-identical regression green (≥100 runs same seed identical) + RTF ≥10x measured.

### Task C1: lifecycle_mgr dual-mode — Shell-B advance-to-next-event clock driver

**Goal:** Shell-B clock driver runs **free-running** (no wall pacing, no `MAX_CATCHUP_TICKS` cap), advancing to the next event gated by the lockstep barrier. Shell A's wall-paced mode preserved behind a parameter.

**Files:** `src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py` — clock callback (367-413), `_MAX_CATCHUP_TICKS` (213, 391-408), `set_sim_rate` (168-172). Add `clock_mode` param: `realtime` (Shell A, existing) | `free_run` (Shell B).

**Test first (red):** `free_run` mode → measured RTF ≫ `sim_rate` (steps/s far above realtime); `MAX_CATCHUP` not applied; `realtime` mode RTF ∈ [0.95,1.05] **unchanged** (existing `test_determinism.py` RTF gate). RED.

**Implementation:**
- [ ] Branch the clock callback by `clock_mode`. `free_run`: advance loop driven by barrier completion (B2), not wall time; emit one `/clock` per `dt_tick`.

**Verify:** RTF ≥10x in `free_run` on `imazu-01-ho`; Shell-A RTF gate still GREEN. **Rollback:** remove `free_run` branch.

### Task C2: In-place `reset(seed)` — clear M1–M8 internal state, no HTTP / no restart

**Goal:** Reset to t=0 without the HTTP lifecycle round-trip or process restart (current cost = seconds/episode). Sub-ms reset enables large RL/MC.

**Files:** `shell_b_harness/` reset orchestrator; add `reset_state(x0, seed, episode)` hooks to each M-node + physics (verify which expose resettable internal state — integrators, filters, covariances; some M-nodes may need a new reset service/entry). M7 reset flows over the B2 deterministic channel (own process).

**Test first (red):** `tests/integration/sim_determinism/test_reset_inplace.py` — (a) cross-episode independence: `reset(seed=S)` then run == a fresh run with `seed=S` (episode N independent of N-1); (b) reset latency < 1ms (exclude first-run init). RED.

**Implementation:**
- [ ] Per-node reset entry clears internal state + re-derives RNG with `episode` (A1).
- [ ] Orchestrator broadcasts reset over the deterministic channel — no DDS discovery, no `Popen`.

**Verify:** cross-episode independence + sub-ms reset GREEN. **Rollback:** fall back to lifecycle cleanup/configure reset.

### Task C3: Headless mode — drop HMI / WebSocket throttle publishers

**Goal:** Drop HMI/WebSocket sinks + publish throttles in Shell B for max throughput, while still tapping state/behavior for capture.

**Files:** `shell_b_harness/` composition profile (exclude M8 HMI WebSocket / foxglove-bridge / bridge UI pubs); `ship_dynamics`/`target_vessel` publish-every-step (A3 flag).

**Test first (red):** headless run launches NO HMI/WebSocket process; RTF higher than non-headless; state/behavior still captured for MC. RED.

**Implementation:** composition profile excludes HMI sinks; keep data taps (`/sil/own_ship_state`, `/l3/m4/behavior_plan`, etc.) for capture.

**Verify:** no HMI process in headless; RTF ≥ target. **Rollback:** enable HMI profile.

> **Phase C gate:** bit-identical regression GREEN (≥100 runs same seed identical) + RTF ≥10x measured + sub-ms reset.

---

## Phase D — Monte-Carlo Harness

> **Phase gate:** MC reproducibility (same seed matrix → identical results twice) + vectorized multi-process runs green.

### Task D1: Gymnasium `Env` wrapper over Shell B

**Goal:** `MASSL3Env(gymnasium.Env)` over Shell B with the standard contract: `reset(seed, options)->(obs, info)`, `step(action)->(obs, reward, terminated, truncated, info)`, `close()`, `observation_space`/`action_space`.

> **[TBD-rl-action-semantics]** — what the RL *action* controls is a genuine design sub-decision, because M1–M8 is the SUT (not replaced). Candidates: (a) scenario/parameter perturbation (adversarial/stress sampling); (b) tuning hooks (e.g. M4 IvP weights, M5 cost params) the stack consults; (c) a learned colav policy benchmarked *against* the stack. **Resolve at Phase-D start with the user's RL objective.** MVP default for the wrapper smoke = (a) scenario-parameter action + own/target-state observation. Reason: lets the env land + be `check_env`-validated without committing the objective; closure path = user picks objective. Blocking: low (wrapper shape is stable across choices).

**Files:** new `src/rl_workbench/envs/massl3_env.py`.

**Test first (red):** `gymnasium.utils.env_checker.check_env(MASSL3Env())` passes; `reset(seed=S)` reproducible; a random-policy rollout runs to termination. RED.

**Implementation:**
- [ ] `super().reset(seed=seed)` as the first line of `reset`; seed `action_space` separately (`action_space.seed(seed)`).
- [ ] Map `step`/`reset` onto Shell-B step + C2 in-place reset; define MVP obs (own/target state + COLREG context) and MVP action (scenario-parameter perturbation); document.

**Verify:** `check_env` GREEN; rollout GREEN. **Rollback:** remove env.

### Task D2: coverage_cube / farn → real-stack (Shell B) dispatch

**Goal:** Drive the existing 1100-cell coverage cube through the REAL stack (Shell B) instead of the open-loop `simulate.py` geometric checker.

**Files:** `tools/sil/coverage_cube.py`, `tools/sil/batch_runner.py`; new adapter calling Shell B per cell (keep cube/seed indexing `seed_index_from_filename`).

**Test first (red):** a 2-cell mini-cube runs through Shell B, produces per-cell metrics, reproducible across two runs. RED.

**Implementation:** replace the `simulate()` / `_simulate_geometric` call path with a Shell-B episode per cell; preserve the cube + seed indexing.

**Verify:** mini-cube reproducible. **Rollback:** keep `simulate.py` path (it remains valid for geometric solvability checks).

### Task D3: Process-level parallel MC + independent `SeedSequence` streams

**Goal:** N worker processes, each an independent `SeedSequence` stream (no cross-talk), reproducible aggregate.

**Files:** new `src/rl_workbench/mc/runner.py` (or extend `batch_runner.py`); `SeedSequence(root).spawn(N)` per worker; one Shell-B per worker.

**Test first (red):** same seed matrix → identical aggregate twice; parallel workers produce independent streams (no shared global RNG); N=4 scaling smoke. RED.

**Implementation:** `multiprocessing` / `SubprocVecEnv`-style; per-worker root from the `spawn_key`.

**Verify:** reproducibility + independence GREEN. **Rollback:** serial runner.

### Task D4: Result aggregation (CPA/DCPA/TCPA, collision rate, COLREG score)

**Goal:** Aggregate per-episode safety/compliance metrics: CPA/DCPA/TCPA, collision rate `(1/M)·Σ Iₘ`, weighted COLREG-compliance score with delayed/non-apparent-maneuver penalties.

**Files:** new `src/rl_workbench/mc/metrics.py`.

**Test first (red):** metrics on a known synthetic episode set match hand-computed values; collision-rate formula correct. RED.

**Implementation:** vectorized over episodes; reuse M2 CPA logic read-only where possible.

**Verify:** metric unit tests GREEN. **Rollback:** remove module.

> **Phase D gate:** MC reproducibility (same seed matrix → identical twice) + vectorized multi-process runs GREEN.

---

## Phase E — RL Workbench + Certification Evidence

> **Phase gate:** all DoD (§6 of spec) green.

### Task E1: `rl_workbench` skeleton (Gymnasium + SB3 smoke)

**Goal:** Minimal RL training loop (SB3 PPO) over `MASSL3Env` runs a few steps headless, reproducibly, including the vectorized env.

**Files:** `src/rl_workbench/` (train script + config + README); keep RL deps optional (extra group, not a hard project dep).

**Test first (red):** SB3 PPO `.learn(few steps)` runs on the env without error; seeded → reproducible; `SubprocVecEnv` works. RED.

**Implementation:** thin train entry over D1's env + D3's parallel streams; document obs/action contract.

**Verify:** smoke GREEN. **Rollback:** remove skeleton.

### Task E2: IEC 61508 T2 tool-qualification doc + 1x Shell-B↔Shell-A cross-validation suite

**Goal:** Certification-defensible evidence: the shared-core argument (Shell B runs the same decision logic, only transport/scheduler differ) + a sampled 1x Shell-B↔Shell-A cross-validation.

**Files:** new doc under `docs/Design/Phase 1/D1.5-vv-plan-scenario-qual/` (verify path; else `docs/Design/Cert/`) — follow CLAUDE.md §7 doc rules + `[Rx]` reference discipline; new `tests/integration/sim_determinism/test_shell_cross_validation.py`.

**Test first (red):** cross-validation suite runs a sampled scenario set on BOTH shells @1x, asserts within tolerance (pos<1m / hdg<0.1° / behavior+conflict identical). RED.

**Implementation:** doc per IEC 61508-3 §7.4.4 (T2 operational spec + simulator FMEA/HazOp + TQSK validation suite); the cross-val test as qualified-by-correlation evidence.

**Verify:** cross-val GREEN; doc reviewed. **Rollback:** doc/test only.

> **Phase E gate:** all DoD (spec §6) GREEN.

---

## Dependency & Parallelization

**Dependency edges:**
- `A0 → A1` (A1 needs the factory). `A2, A3, A5, B0` are independent (of A0 and of each other). `A4` depends on `A2 + A3` (shared files).
- All of Phase A → Phase B/C bit-identity.
- `B0 → B3` (the spike selects the path). `B1 → B2 → B3`. (B1 benefits from A4 but may start once A3/A4 land.)
- `C1` needs `B1 + B2`. `C2` needs `C1`. `C3` needs `C1`.
- `D1` needs `C1 + C2`. `D2 → D1`. `D3 → D1`. `D4` develops against fixtures, integrates after D1.
- `E1` needs `D1 (+ D3)`. `E2` needs Phase B (shared-core) + Phase C (1x parity).

**Parallel lanes (disjoint owned files — safe for separate agents/sessions):**

| Wave | Lane | Task | Owns (files) |
|---|---|---|---|
| 1 | L0 | A0 | `src/sim_workbench/sil_common/det_rng.py`, `tests/unit/test_det_rng.py` |
| 1 | L-steer | A2 | `docker/sil_topic_bridge.py` (LATCH/throttle), rule14 capture test |
| 1 | L-phys | A3 | `ship_dynamics/ship_dynamics/node.py`, `mmg_model.py`, dt unit test |
| 1 | L-build | A5 | `fcb_simulator/CMakeLists.txt`, `colcon.meta`, flag test |
| 1 | L-spike | B0 | `tests/integration/sim_determinism/test_physics_core_equiv.py` + decision memo (read-only on prod) |
| 2 | L-rng-{tv,sm,env,fi} | A1 ×4 | one of `target_vessel`/`sensor_mock`/`env_disturbance`/`fault_injection` `.../node.py` each |
| 3 | (serial) | A4 | merges after L-steer + L-phys (shares their files) |

**Collision guards (must obey):**
- `docker/sil_topic_bridge.py`: A2 **and** A4 → run A4 after A2.
- `ship_dynamics/node.py`: A3 **and** A4 → run A4 after A3.
- `lifecycle_bridge.py` `root_seed` injection is shared by the 4 A1 sub-lanes → do that one-line injection as a tiny **serial pre-step** (or assign to exactly one sub-lane) so the 4 node sub-lanes stay disjoint.
- Phase B onward is mostly **serial** (B1→B2→B3; C1→{C2,C3}; D1→{D2,D3}; D4 parallel-against-fixtures) — execute with `superpowers:subagent-driven-development`, not raw parallel dispatch.

---

## Parallel Dispatch Prompts (appendix)

> Paste one per new Claude Code session. Each is self-contained. All assume: `cd` to repo, **branch off `feat/sim-speed-determinism`** into a worktree (`superpowers:using-git-worktrees`), use `superpowers:test-driven-development`, no fake stubs, verify before claiming done.

**Wave-1 lanes can run concurrently. Do NOT start Wave-2 (A1) until A0 is merged.**

### Prompt — Lane L0 / Task A0 (seeded-RNG factory)
```
Repo: /Users/marine/Code/MASS-L3-Tactical Layer. Branch off feat/sim-speed-determinism into a worktree.
Read docs/superpowers/plans/2026-05-30-sim-superrealtime-deterministic-mc-rl-base-plan.md Task A0 and the spec it references (§3.5). Use TDD.
Build a shared seeded-RNG factory at src/sim_workbench/sil_common/det_rng.py exposing make_rng(root, episode, node, worker) -> np.random.Generator backed by np.random.SeedSequence(entropy=[root, episode, NODE_ID[node], worker]). Write tests/unit/test_det_rng.py FIRST (red): identical keys → identical sequence; any differing key → different; cross-process reproducibility via pickled key. Then implement to green.
Constraints: only these two files. Do not touch other nodes (that is Task A1). Verify: pytest tests/unit/test_det_rng.py -q green. Return: summary + diff stat.
```

### Prompt — Lane L-steer / Task A2 (steering off wall clock)
```
Repo: /Users/marine/Code/MASS-L3-Tactical Layer. Branch off feat/sim-speed-determinism into a worktree.
Read the plan's Task A2 + spec §2.4#2. Use TDD. Problem: docker/sil_topic_bridge.py drives the avoidance LATCH heading-decay and actuator throttle on time.monotonic() (lines ~450,465,568-572, _autopilot_step ~294), so the maneuver differs across sim speeds. Make the steering command a pure function of SIM time (self.get_clock().now(); the node runs use_sim_time:=True).
Test first (red): extend a turning-scenario capture (colreg-rule14-ho) to assert the heading-command trajectory is identical at 1x vs 10x at aligned sim_t. Then fix to green.
Constraints: only docker/sil_topic_bridge.py (+ the test). Do NOT change Shell A's realtime behavior. Verify: 1x-vs-10x heading match within tolerance; existing tests/integration/sim_determinism/test_determinism.py still green. Return: summary + diff stat.
```

### Prompt — Lane L-phys / Task A3 (ship_dynamics dt consistency)
```
Repo: /Users/marine/Code/MASS-L3-Tactical Layer. Branch off feat/sim-speed-determinism into a worktree.
Read the plan's Task A3 + spec §2.4#5. Use TDD. Problem: ship_dynamics/ship_dynamics/node.py integrates with mmg_model.c.dt (mmg_model.py:94) but advances the clock/step-count with a hardcoded 0.02 (node.py:268,288); also it wall-throttles publishing (313-318).
Test first (red): unit asserting a single dt source (self._model.c.dt) drives BOTH step count and clock advance; with injected dt != 0.02, integrated sim-time == clock-advanced sim-time. Then fix. Add a `headless` flag that publishes every step (Shell B), keeping the monotonic throttle for Shell A.
Constraints: only ship_dynamics/node.py (+ mmg_model.py if needed) + test. Verify: unit green; Shell A publish rate unchanged. Return: summary + diff stat.
```

### Prompt — Lane L-build / Task A5 (FP determinism flags)
```
Repo: /Users/marine/Code/MASS-L3-Tactical Layer. Branch off feat/sim-speed-determinism into a worktree.
Read the plan's Task A5 + spec §3.5#4. Goal: guarantee fixed float op results for cross-host bit-identity. Add -fno-fast-math (and -ffp-contract=off) to the C++ physics + in-loop-math decision targets via target_compile_options / colcon.meta; ensure NO -ffast-math exists anywhere.
Test first (red): a build-config assertion test (grep: no -ffast-math; -fno-fast-math present on the right targets). Then add flags.
HARD CONSTRAINT: per CLAUDE.md §12, do NOT remove or alter Docker BuildKit `--mount=type=cache` lines or the `# syntax` directive. Verify: colcon build green + flag test green. Return: summary + diff stat.
```

### Prompt — Lane L-spike / Task B0 (physics path decision spike — READ-ONLY on prod)
```
Repo: /Users/marine/Code/MASS-L3-Tactical Layer. Branch off feat/sim-speed-determinism into a worktree.
Read the plan's Task B0 + spec §3.4. This is a DECISION SPIKE — produce a real artifact, change no production code.
1) Determine whether src/sim_workbench/fcb_simulator/python/fcb_sim_py is a REAL pybind11 binding to the C++ core or falls back to fcb_sim_py_mock.py (note tools/sil/fcb_sim_py_mock.py on path). Confirm what tools/sil/simulate.py imports at runtime.
2) Compare the C++ fcb_simulator MMG vs Python mmg_model.py: drive both with an identical (state, command, dt) sequence; measure trajectory delta. Write tests/integration/sim_determinism/test_physics_core_equiv.py.
3) Decision: real binding AND delta ≤ (pos<1m, hdg<0.1°) → P1 (unify on C++); mock OR delta too large → P3 (pure step-fns embedded in DOER); P2 only if cross-language infeasible.
Output: append a decision memo (chosen path + measured delta + evidence) to the plan file under Task B3. Return: the decision + the memo text.
```

### Prompt — Wave-2 / Task A1 sub-lane (per node; run ONE node per session, after A0 merged)
```
Repo: /Users/marine/Code/MASS-L3-Tactical Layer. Branch off feat/sim-speed-determinism (with A0 merged) into a worktree.
Read the plan's Task A1 + spec §3.5#1. Use TDD. Replace global `random.` in ONE node only: <NODE = target_vessel | sensor_mock | env_disturbance | fault_injection> at src/sim_workbench/sil_nodes/<NODE>/.../node.py with a per-node seeded Generator from src/sim_workbench/sil_common/det_rng.make_rng (built in on_configure from a root_seed param; re-derived per episode on reset).
Map: random.gauss(0,1)->rng.normal(); random.random()->rng.random(); random.uniform(a,b)->rng.uniform(a,b); random.randint(a,b)->rng.integers(a,b+1).
Test first (red): same seed twice → identical noise; different seed → different; assert no `random.` survives in this node file. Then fix to green.
Constraints: touch ONLY this node's file (+ test). The root_seed plumb into lifecycle_bridge.py is a separate shared step — do NOT edit lifecycle_bridge.py here. Verify: new test green + existing test_determinism.py green. Return: summary + diff stat.
```

> **Phase B–E** are executed sequentially in one driving session via `superpowers:executing-plans` / `superpowers:subagent-driven-development` (B2's cross-process deterministic barrier is the hardest novel piece — keep it in a focused session, not a parallel lane).

---

## Verification Strategy

| Gate | Test (new unless noted) | Pass criterion |
|---|---|---|
| Bit-identical | `test_bit_identical.py` | A scenario incl. RNG (e.g. NCDM target) run ≥100× same seed in Shell B → identical trajectory hash; cross-host on ≥2 archs in CI → identical |
| Shell-A no-regression | existing `test_determinism.py` + `.preflight/gate_*.json` | RTF(1x) ∈ [0.95,1.05]; behavior/conflict unchanged |
| Throughput | `test_shell_b_rtf.py` | free_run RTF ≥10x (aim 50x); report steps/s |
| Reset | `test_reset_inplace.py` | <1ms reset + cross-episode independence (N ⟂ N-1) |
| MC reproducibility | `test_mc_reproducibility.py` | same seed matrix → identical twice; parallel-stream independence |
| Cross-validation | `test_shell_cross_validation.py` | sampled set, both shells @1x within tolerance (pos<1m/hdg<0.1°/behavior+conflict identical) |
| Build | colcon | green; BuildKit cache mounts intact (CLAUDE.md §12) |

**Running the docker-bound integration tests** (per spec App. D): `docker compose up` → lifecycle API on HTTPS:8000 (self-signed, skip verify) → cleanup→configure→rate→activate→capture→deactivate; capture inside `sil-nodes` container (`docker exec`, source ROS + `/opt/ws/install/setup.bash`).

---

## Self-Review

- [x] Every task: failing-test-first, exact files+anchors, verification command, rollback note.
- [x] No bare TBD in implementation steps. The two genuine forks — physics path (B0) and RL action semantics (D1 `[TBD-rl-action-semantics]`) — are framed as decision tasks with measurable criteria + closure paths (project `[TBD-<reason>]` discipline), not placeholders.
- [x] **ADR#2 M7 isolation preserved** (B2 = lockstep barrier across processes, never a merge; flagged forbidden). **ADR#4** no vessel constants in core.
- [x] Shell-A no-regression gate present in every relevant phase; dual-mode behind a parameter.
- [x] No fake stubs: B0/B1 "minimal entries" are real (a working sync-step + a real equivalence test), extended later — not NotImplementedError.
- [x] Parallel lanes own disjoint files; collision guards documented.
- **Highest-uncertainty / watch items:** (1) **B2 deterministic cross-process barrier** — RSLCPP [R1] is single-process; our M7-isolation barrier is an extension with no in-repo precedent → highest risk, prototype early. (2) **P1 viability** hinges on B0 (fcb_sim_py may be a mock). (3) **C2 per-node reset hooks** may require adding reset entries to M-nodes — keep to state-clear/construction, never decision logic. (4) Cross-host bit-identity (A5) is unproven for this stack until Phase C CI runs it.
