# DEMO-1 Dry-Run Report — 2026-05-20

**Owner:** V&V Engineer + Technical Lead + M8 Frontend  
**Milestone:** DEMO-1 Skeleton Live (6/15)  
**Scenario:** imazu-08-ms (Rule 14 head-on, own-give-way)  
**Execution date:** 2026-05-20  

---

## 1. Scope

| In scope | Out of scope |
|---|---|
| verify_demo1_e2e.sh R1a–R1f green | ToR Modal hard-acceptance (DEMO-3) |
| Playwright 6/6 core cases green | SAT-2/SAT-3 data validation (DEMO-2 P0) |
| 5-min visual dry-run (browser) | ASDR take_over validation (DEMO-2/3) |
| No DEMO MODE red badge | |
| No fatal log lines | |

---

## 2. Infrastructure Blockers (identified 2026-05-20)

### B-BLOCK-01: sil-nodes Docker image predates L3 kernel integration

**Symptom:** Running container `mass-l3-tacticallayer-sil-nodes-1` has 11 SIL nodes only (no M1–M8); `/opt/ws/install/` does not contain L3 packages.

**Root cause:** Docker image was built before `COPY src/l3_tdl_kernel` layer was added to `docker/sil_nodes.Dockerfile`. The pre-built image (1.09 GB) only contains `sim_workbench` packages.

**Resolution:** `docker compose build sil-nodes` — full rebuild required (colcon build for all L3 C++ and Python packages). Build started 2026-05-20 ~17:55 UTC+8.

**Impact on this report:** Sections 3.1 R1 deep validation and 3.2 Playwright run pending build completion.

---

## 3. Verification Results

### 3.1 verify_demo1_e2e.sh

**Run command:**
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer
bash scripts/verify_demo1_e2e.sh --scenario-yaml scenarios/IMAZU标准测试/imazu-08-ms.yaml
```

**Baseline (Stages 1–3): pre-L3 build**

| Check | Result | Note |
|---|---|---|
| /sil/own_ship_state first frame | ✅ PASS | Published within 5s of activate |
| 11 SIL nodes (pre-build) | ✅ PASS | ship_dynamics + 8 SIL infra nodes |
| /sil/own_ship_state hz | ✅ PASS | ~50 Hz (Stage 1 confirms) |
| 16+ nodes (L3 required) | ❌ BLOCKED | Pending Docker rebuild |

**R1 deep validation:** ❌ BLOCKED pending Docker rebuild

JSON evidence path: `evidence/demo1-e2e-verification-{timestamp}.json` (generated after 3× green run)

---

### 3.2 Playwright — 6 core cases

**Run command:**
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/web
npx playwright test e2e/demo1-headon-r14.spec.ts \
  --reporter=html \
  --output=../evidence/playwright-report-$(date +%Y%m%dT%H%M%SZ)/
```

**Out-of-scope tests skipped:**  
- `test.skip('ToR Modal triggers on R14 head-on encounter', ...)` — DEMO-3  
- `test.skip('ASDR Ledger receives take_over event after TOR→OVERRIDE', ...)` — DEMO-2/3  

**6 core cases status:** ❌ BLOCKED — requires L3 nodes for M8 telemetry + module pulse

| # | Test | Required | Status |
|---|---|---|---|
| 1 | own-ship telemetry populates within 30s | /sil/own_ship_state via WS | ⏳ |
| 2 | target vessel BRG/RNG in ArpaTargetTable | /sil/tracked_targets via WS | ⏳ |
| 3 | ModulePulseBar M1–M8 heartbeat dots | /sil/module_pulse (needs M7+bridge) | ⏳ |
| 4 | ConningBar 7 fields (HDG/COG/SOG/ROT/RUD/RPM/PITCH) | /sil/m8_ui_state (needs M8) | ⏳ |
| 5 | ThreatRibbon CPA chips | /sil/tracked_targets + CPA from L3 | ⏳ |
| 6 | WS CONNECTED / no DEMO MODE badge | foxglove bridge live | ⏳ |

HTML report path: `evidence/playwright-report-{timestamp}/`

---

### 3.3 5-min Visual Dry-Run Checklist

**Prerequisites:** Docker rebuild complete + `docker compose up -d` + scenario activated

**URL:** `http://localhost:5173/#/monitor/imazu-08`  
**Duration:** 5 min  
**Check interval:** every 60s

| t (min) | Check | Expected | Result |
|---|---|---|---|
| 0 | WS CONNECTED badge (teal) visible | Yes, within 10s | ⏳ |
| 0 | DEMO MODE red badge absent | Absent | ⏳ |
| 1 | own_ship moves on MapLibre (lat/lon changes) | Curved track heading North | ⏳ |
| 1 | target_ship initial lat≈63.50/lon≈10.24 (ts1), lat≈63.57 (ts2) | Matches scenario.yaml | ⏳ |
| 2 | ConningBar 7 fields flowing (not frozen) | HDG/COG/SOG/ROT/RUD/RPM/PITCH changing | ⏳ |
| 3 | ThreatRibbon CPA chip color | Green→Amber as vessels converge | ⏳ |
| 4 | ModulePulseBar 8 slots GREEN | M1–M8 all green, M7 ≥ 5 Hz | ⏳ |
| 5 | No DEMO MODE banner at end | Absent throughout | ⏳ |

Screen recording: `evidence/demo1-dryrun-2026-05-20.mp4` (manual step — connect via OBS or QuickTime)

---

## 4. Script Changes (delivered)

### 4.1 scripts/verify_demo1_e2e.sh — extensions

**New check functions added:**

| Function | Verifies |
|---|---|
| `check_hz_min desc topic min_hz` | R1a: Hz ≥ threshold via `ros2 topic hz --window 5` |
| `check_param_float desc node param expected` | R1b: float param matches scenario YAML value ±0.0001 |
| `check_topic_nonempty id topic` | R1c/R1d/R1e: `timeout 10 ros2 topic echo --once` returns a message |
| `check_no_fatal_logs desc service` | R1f: `docker compose logs sil-nodes` contains no 'fatal' lines |

**New R1 checks ([4/4] stage):**

| ID | Check | Validates |
|---|---|---|
| R1a | `/sil/own_ship_state` ≥ 40 Hz | ship_dynamics actively publishing (B1 wait-for-ready) |
| R1b | `/ship_dynamics_node initial_lat` = 63.44 | B2 scenario_id parameter injection |
| R1c | `/l3/m4/behavior_plan` echo non-empty | M4 BehaviorArbiter running (not stub) |
| R1d | `/l3/m5/avoidance_plan` echo non-empty | M5 TacticalPlanner running (not stub) |
| R1e | `/l3/m7/heartbeat` echo non-empty | M7 SafetySupervisor heartbeat alive |
| R1f | `docker compose logs sil-nodes` no fatal | B3 fail-loud default not triggered |

**JSON report output:** `evidence/demo1-e2e-verification-{timestamp}.json`

### 4.2 web/e2e/demo1-headon-r14.spec.ts — skip annotations

Two out-of-scope tests annotated with `test.skip()`:
- ToR Modal → DEMO-3 hard-acceptance
- ASDR Ledger take_over → DEMO-2/3

---

## 5. Pending Steps (after Docker rebuild completes)

```bash
# 1. Restart sil-nodes with new image
docker compose up -d --force-recreate sil-nodes foxglove-bridge

# 2. Wait for staged startup (Stage 1 → Stage 2 → Stage 3)
sleep 30

# 3. Configure + activate imazu-08-ms
curl -X POST http://localhost:8000/api/v1/lifecycle/configure \
  -H 'Content-Type: application/json' -d '{"scenario_id": "imazu-08-ms"}'
curl -X POST http://localhost:8000/api/v1/lifecycle/activate \
  -H 'Content-Type: application/json' -d '{}'

# 4. Wait for L3 nodes (Stage 3 ~15s)
sleep 20

# 5. Run verify script (3 consecutive runs)
for i in 1 2 3; do
  echo "=== Run $i/3 ===" && bash scripts/verify_demo1_e2e.sh \
    --scenario-yaml scenarios/IMAZU标准测试/imazu-08-ms.yaml
done

# 6. Run Playwright
cd web && npx playwright test e2e/demo1-headon-r14.spec.ts \
  --reporter=html \
  --output=../evidence/playwright-report-$(date +%Y%m%dT%H%M%SZ)/

# 7. Open browser for 5-min dry-run
open http://localhost:5173/#/monitor/imazu-08
```

---

## 6. Evidence Index

| Artifact | Path | Status |
|---|---|---|
| E2E verify JSON (run 1) | `evidence/demo1-e2e-verification-*.json` | ⏳ post-rebuild |
| E2E verify JSON (run 2) | `evidence/demo1-e2e-verification-*.json` | ⏳ post-rebuild |
| E2E verify JSON (run 3) | `evidence/demo1-e2e-verification-*.json` | ⏳ post-rebuild |
| Playwright HTML report | `evidence/playwright-report-*/` | ⏳ post-rebuild |
| 5-min screen recording | `evidence/demo1-dryrun-2026-05-20.mp4` | Manual |
| Docker build log | `docker compose build sil-nodes` stdout | In-progress |

---

*Report auto-generated by V&V session — 2026-05-20. Update §3 entries once Docker rebuild + rerun completes.*
