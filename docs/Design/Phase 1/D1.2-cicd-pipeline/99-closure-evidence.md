# D1.2 CI/CD Pipeline — Closure Evidence

**Date**: 2026-05-09  
**Commit range**: `07fa25d` → current  
**Closed by**: 技术负责人

---

## Stage 1: Lint (local simulation)

| Job | Result | Evidence |
|---|---|---|
| `ruff` | ✅ PASS | `ruff check src/ tests/` → 0 violations |
| `multi_vessel_lint` | ✅ PASS | 0 hits (`FCB`/`45 m`/`18 kn`/`22 kn`/`ROT_max`/`sog <=`) in `src/*.py` |
| `path_s_dry_run` (Python) | ✅ PASS — OK: 0 violations | `grep -rn "from m7_safety_supervisor" src/ | grep -v m7/` → 0 |
| `path_s_dry_run` (C++) | ✅ PASS | `check-doer-checker-independence.sh` → "Doer-Checker independence: OK" |
| `commit-msg` | ✅ PASS | `check-commit-msg.sh` invoked directly, no `|| echo` |

`path_s_dry_run` retains `allow_failure: true` per plan (→ `false` after 6/15 DEMO-1).

---

## Stage 2: Unit Test

| Job | Result | Evidence |
|---|---|---|
| `stage-2-pytest` | ✅ 39 passed | `python3 -m pytest tests/ -q` — 39 passed, 0 failed, 0 errors |

Test breakdown:
- `tests/m2/` — encounter classifier, track validator
- `tests/m5/` — MPC params, fallback policy
- `tests/m8/` — active_role (10 tests)
- `tests/common/` — capability manifest

---

## Stage 3: Static Analysis

| Job | Result | Evidence |
|---|---|---|
| `stage-3-doer-checker-independence` | ✅ PASS | `check-doer-checker-independence.sh` → "Doer-Checker independence: OK" |
| `stage-3-clang-tidy` | ✅ PASS (container) | commit `3808c49` — all 15 packages compile; commit `33f6905` — clang-tidy exit 0 with NOLINTNEXTLINE precision suppressions (see §9.1 in spec) |

---

## Stage 4: Integration

| Job | Result | Evidence |
|---|---|---|
| `stage-4-integration` (colcon build) | ✅ PASS (container) | commit `3808c49` `fix(d1.2): build-green — full workspace compiles in CI Docker (ARM64)` — all 15 packages, exit 0 |

---

## Stage 5: Release

| Job | Result | Evidence |
|---|---|---|
| `stage-5-release` (SBOM) | ✅ SKIP (expected) | `rules: if: $CI_COMMIT_TAG` — no release tag on dev branch; SBOM job correctly not triggered |

---

## Docker Build Reproducibility

From `build-ci.log` (2026-05-08):

```
Dockerfile.ci: exit 0, digest sha256:5eafc4e2306e693ee0c0004575f1abef0efbf75b22eb670317a0a0a64dcc6275
Dockerfile.dev: exit 0, digest sha256:9a803e5a749b5772aea531609753c0ff6eb95d4448ef04ffb0f45df24b6be6cb
Digest determinism: PASS (second build matches first)
```

---

## Finding A P1-A8 Closure

`path_s_report.txt` (local simulation):
```
Doer→Checker violations (Python): 0
C++ Doer→Checker violations: 0
OK: No Doer→Checker violations (Python + C++)
```

**A P1-A8 CLOSED.**

---

## Structural Changes (2026-05-09)

| Change | Reason |
|---|---|
| `src/m8_hmi_bridge/` removed | Misnamed legacy directory; `active_role.py` migrated to `src/m8_hmi_transparency_bridge/python/web_server/active_role.py` |
| `tests/m8/test_active_role.py` import updated | `m8_hmi_bridge.active_role` → `web_server.active_role` |
| `pytest.ini` pythonpath extended | Added `src/m8_hmi_transparency_bridge/python` so `web_server` package is importable |
| `sil_mock_publisher/sil_mock_node.py` | ruff I001 import sort fixed |
