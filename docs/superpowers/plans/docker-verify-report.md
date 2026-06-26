# Docker Compose & CI Pipeline Verification Report

**Generated**: 2026-05-19
**Method**: Manual audit of docker-compose.yml, all 4 Dockerfiles, entrypoint script, tile data, and .gitlab-ci.yml
**Auditor**: Sisyphus-Junior (AVDS Expert)

---

# PART 2 — Docker Compose Verification

## 2.1 File Structure

| File | Exists | Lines | Status |
|------|--------|-------|--------|
| `docker-compose.yml` | ✅ | 78 | GOOD |
| `docker/sil_orchestrator.Dockerfile` | ✅ | 39 | GOOD |
| `docker/sil_nodes.Dockerfile` | ✅ | 38 | GOOD |
| `docker/ci.Dockerfile` | ✅ | 28 | GOOD |
| `web/Dockerfile` | ✅ | 17 | GOOD |
| `docker/sil_entrypoint.sh` | ✅ | 67 | GOOD |

All 6 files exist. Dockerfile syntax check (`docker buildx build --check`) passed with no warnings.

## 2.2 Service Verification

| # | Service | Base Image | Port(s) | Network | Status |
|---|---------|-----------|---------|---------|--------|
| 1 | **sil-orchestrator** | `mass-l3/ci:humble-ubuntu22.04` | 8000 | host | ✅ |
| 2 | **sil-nodes** | `ros:humble-ros-base` | — | host | ✅ |
| 3 | **foxglove-bridge** | `mass-l3/ci:humble-ubuntu22.04` | 8765 | sil-nodes | ✅ |
| 4 | **web** | `node:20-alpine` | 5173 | default | ✅ |
| 5 | **martin-tile-server** | `ghcr.io/maplibre/martin:latest` | 3000 | host | ✅ |

**All 5 required services present.** ✓

### 2.2.1 foxglove-bridge Configuration

| Parameter | Value | Status |
|-----------|-------|--------|
| Port | 8765 | ✅ Required port |
| num_threads | 4 | ✅ |
| send_buffer_limit | 10,000,000 | ✅ |
| use_sim_time | true | ✅ |
| Topic whitelist | `[/sim_clock, /sil/own_ship_state, /sil/target_vessel_state, /sil/radar_meas, /sil/ais_msg, /sil/environment, /sil/tracked_targets, /sil/lifecycle_status, /sil/module_pulse, /sil/scoring, /sil/asdr_event]` | ✅ 11 SIL topics |

**foxglove_bridge correctly configured.** ✓

### 2.2.2 martin-tile-server

| Check | Result | Status |
|-------|--------|--------|
| Image | `ghcr.io/maplibre/martin:latest` | ✅ |
| Volume mount | `./data/tiles:/data/tiles` | ✅ |
| Command | `["/data/tiles"]` | ✅ |
| Tile data exists | ✅ | See §2.3 |

### 2.2.3 Volume Mounts & Data Persistence

| Volume | Type | Paths |
|--------|------|-------|
| `./scenarios:/var/sil/scenarios` | bind (orchestrator + nodes) | Read scenario YAML |
| `./runs:/var/sil/runs` | bind (orchestrator + nodes) | Write run outputs |
| `./exports:/var/sil/exports` | bind (orchestrator only) | Write scenario exports |
| `./data/tiles:/data/tiles` | bind (martin only) | MVT tile data |

## 2.3 Tile Data Verification

| File | Size | Type |
|------|------|------|
| `data/tiles/trondelag.mbtiles` | ~906 MB | Production tileset (Trondheim) |
| `data/tiles/test.mbtiles` | ~1.6 MB | Test tileset |
| `data/tiles/trondelag.geojson` | ~13.5 MB | Source GeoJSON |
| `data/tiles/geojson/` (dir) | 704 B | Additional GeoJSON files |

**Total tile files: 23** (as reported by `find data/tiles -type f | wc -l`)

The D1.3b.3-DoD-CHECKLIST confirms: "S-57 -> MVT pipeline: Trondheim tiles generated (37,136 tiles, Z6-Z14)". ✓

## 2.4 RMW Configuration

All ROS2 services use:
- `ROS_DOMAIN_ID=0`
- `RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`

The `sil_nodes` container installs `ros-humble-rmw-cyclonedds-cpp` at runtime
via the `command` overrides (both in `docker-compose.yml` and the Dockerfile).

**Note**: Runtime install of CycloneDDS in `command` is fragile. Consider baking it
into `sil_nodes.Dockerfile` for production stability. ⚠️

## 2.5 Docker Compose Summary

```
docker-compose.yml: PASS ✓
  - 5/5 services present
  - foxglove_bridge: port 8765, 11-topic whitelist, sim_time ON
  - martin-tile-server: trondelag.mbtiles (906 MB, 37K tiles, Z6-Z14)
  - web: Vite dev server on port 5173
  - sil-orchestrator: FastAPI on port 8000
  - sil-nodes: 9 LifecycleNode cluster via entrypoint
  - All Dockerfile syntax checks PASS
```

---

# PART 3 — CI Pipeline Verification

## 3.1 Pipeline Overview

**File**: `.gitlab-ci.yml` (668 lines)

### Stage Declaration

```
stages:
  - lint
  - build
  - unit-test
  - test
  - static-analysis
  - integration-test
  - release
  - sil-smoke
  - sil-baseline
```

### 5 Required Stages

| # | Stage | Exists | Job Definition | Status |
|---|-------|--------|---------------|--------|
| 1 | `lint` | ✅ | `stage-1-lint` — clang-format + clang-tidy diff | ✅ |
| 2 | `build` | ✅ | `stage-2-build` — colcon build with ccache | ✅ |
| 3 | `unit-test` | ✅ | `stage-2-unit-test` — colcon test per package | ✅ |
| 4 | `static-analysis` | ✅ | `stage-3-*` — clang-tidy full, cppcheck MISRA, Polyspace (matrix: m1, m7), TSAN (m2, m5), doer-checker independence | ✅ |
| 5 | `integration-test` | ✅ | `stage-4-integration` — colcon full build + launch L3 stack + GTest + pytest + msg contract check | ✅ |

**All 5 required stages present.** ✓

### Additional Stages

| Stage | Job | Purpose | Status |
|-------|-----|---------|--------|
| `release` | `stage-5-release` | colcon Release build + tarball + SBOM | ✅ |
| `sil-smoke` | `sil-smoke` | pytest `test_cutover_parallel_run.py` (every MR) | ✅ |
| `sil-baseline` | `sil-baseline-imazu22` | Imazu-22 benchmark (main only) | ✅ |

## 3.2 sil-smoke Job (Line 630–646)

| Property | Value | Status |
|----------|-------|--------|
| Stage | `sil-smoke` | ✅ |
| Image | `mass-l3/ci:humble-ubuntu22.04` | ✅ |
| Needs | `[]` (no dependencies) | ✅ |
| Script | pytest `test_cutover_parallel_run.py` | ✅ |
| Artifacts | `test-results/` (7-day) | ✅ |
| Triggers | merge_requests, main | ✅ |

**sil-smoke has an actual job definition.** ✓

## 3.3 sil-baseline Job (Line 651–668)

| Property | Value | Status |
|----------|-------|--------|
| Stage | `sil-baseline` | ✅ |
| Image | `mass-l3/ci:humble-ubuntu22.04` | ✅ |
| Needs | `[]` (no dependencies) | ✅ |
| Script | `tools/run_imazu22.py` + `tools/check_entry_gate.py` | ✅ |
| Artifacts | `test-results/`, `runs/` (30-day) | ✅ |
| Triggers | main only | ✅ |

**sil-baseline has an actual job definition.** ✓

## 3.4 CI Base Image Verification

| Check | Result |
|-------|--------|
| Image name | `mass-l3/ci:humble-ubuntu22.04` |
| Base | `ros:humble-ros-base` |
| Dockerfile | `docker/ci.Dockerfile` (28 lines) |
| Syntax check | `docker buildx build --check` — **PASS, no warnings** |
| Content | ROS2 Humble + foxglove-bridge + rosbag2-mcap + colcon-common-extensions + pytest |

The CI image is correctly specified and the Dockerfile syntax is valid. ✓

### Notable Items

1. **Polyspace job** (`stage-3-polyspace`, line 490–540): exits with `exit 1` if Polyspace CLI is not in the CI container. This is expected behaviour (Wave 0 placeholder — see F-CRIT-A-009 in comments).

2. **SBOM generation** (line 615–619): graceful fallback if `syft` is unavailable, with comment noting it must be fixed before CCS AIP submission.

3. **Cache configuration**: ccache (5GB) with pull-push policy across all jobs.

## 3.5 CI Pipeline Summary

```
.gitlab-ci.yml: PASS ✓
  - 9 declared stages (5 required + 4 additional)
  - 5 required stages: lint ✅, build ✅, unit-test ✅, static-analysis ✅, integration-test ✅
  - sil-smoke: pytest job for every MR ✅
  - sil-baseline: Imazu-22 benchmark on main ✅
  - CI base image: mass-l3/ci:humble-ubuntu22.04 Dockerfile syntax valid ✅
  - All jobs have rules/triggers, caching, and artifact handling ✅
```

---

# Overall DEMO-1 Readiness Verdict

| Area | Result | Critical Issues |
|------|--------|----------------|
| Documentation (6 content files) | 🟢 GOOD | V&V Plan, Review, HAZID at wrong paths |
| Documentation (5 stubs created) | 🟡 STUBS NEED CONTENT | Sim qual, Scenario schema, Coverage, Cert, ConOps |
| Docker Compose (5 services) | 🟢 PASS | All services, ports, volumes, topics correct |
| CI Pipeline (5 stages) | 🟢 PASS | All jobs defined; sil-smoke + sil-baseline active |
| **DEMO-1 Gate** | 🟡 **CONDITIONAL PASS** | 5 stubs need content by 6/15; path inconsistencies need resolution |
