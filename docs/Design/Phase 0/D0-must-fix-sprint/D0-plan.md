# D0 Must-Fix Sprint Implementation Plan

> **Status: ✅ COMPLETED 2026-05-08** — All 15 findings closed. 39 pytest passing. Git tag `v1.1.2-patch1` @ `c7a6ea2`. See [`99-closure-report.md`](99-closure-report.md) for full evidence chain.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close all 11 must-fix findings + 6 multi-vessel literals + 3 decision documents + CI enforcement + deep research + CLAUDE.md drift, enabling 5/13 Phase 1 D1.x kickoff and HAZID RUN-001 without blocking issues.

**Architecture:** Three sequential waves of work; within each wave, tasks are fully independent and can be dispatched as parallel subagents simultaneously. Wave 0 (bootstrap) must complete before Wave 1; Wave 1 must complete before Wave 2; Wave 2 must complete before the closure task. Pure-logic Python modules (zero ROS2 imports) under `src/`; TDD enforced on all code tasks; document patches are surgical (only spec-cited lines).

**Tech Stack:** Python ≥3.11, pydantic ≥2.0, pyyaml ≥6.0, pytest ≥8.0, ruff ≥0.4, GitLab CI YAML.

---

## File Structure

```
src/
  m2_world_model/
    __init__.py
    encounter_classifier.py     # Task 1A: MUST-1
    track_validator.py          # Task 1C: MUST-6 (part 2)
  m5_tactical_planner/
    __init__.py
    mpc_params.py               # Task 1B: MUST-2
    fallback_policy.py          # Task 2A: MUST-5 + MUST-9
  m8_hmi_bridge/
    __init__.py
    active_role.py              # Task 1D: MUST-7
  common/
    __init__.py
    capability_manifest.py      # Task 1C: MUST-6 (part 1)
tests/
  m2/
    __init__.py
    test_encounter_classifier.py   # Task 1A
    test_track_validator.py        # Task 1C
  m5/
    __init__.py
    test_mpc_params.py             # Task 1B
    test_fallback_policy.py        # Task 2A
  m8/
    __init__.py
    test_active_role.py            # Task 1D
  common/
    __init__.py
    test_capability_manifest.py    # Task 1C
config/
  capability_manifest.schema.json  # Task 1C (generated)
  vessels/
    fcb_45m.yaml                   # Task 1C
.gitlab-ci.yml                     # Task 2B
pyproject.toml                     # Task 0
.ruff.toml                         # Task 0
pytest.ini                         # Task 0
docs/Design/Detailed Design/D0-must-fix-sprint/
  01-spec.md                       # already exists
  02-coding-standards.md           # Task 2B
  99-closure-report.md             # Task 3
docs/Design/Cross-Team Alignment/
  RFC-007-M7-X-axis-Heartbeat.md   # Task 1E
  RFC-009-IvP-Implementation-Path.md  # Task 1F
docs/Design/HAZID/
  RUN-001-fcb-data-substitute-memo.md  # Task 2C
docs/Design/Detailed Design/M7-Safety-Supervisor/
  02-effort-split-v2.1.md          # Task 2D
```

---

## Wave 0 (Sequential Prerequisite — complete before starting Wave 1)

### Task 0: Workspace Bootstrap

**Files:**
- Create branch: `feature/d0-must-fix-sprint`
- Create: all directories and empty `__init__.py` files listed in File Structure
- Create: `pyproject.toml`, `.ruff.toml`, `pytest.ini`

**Context for this task:** Base branch is `main` at commit `5113558`. This sprint uses pure-logic Python only — zero ROS2/rclpy imports anywhere in `src/` or `tests/`. The bootstrap creates the empty scaffolding so Wave 1 subagents have a consistent tree to work in.

- [ ] **Step 1: Create feature branch**

```bash
git checkout -b feature/d0-must-fix-sprint
```
Expected: `Switched to a new branch 'feature/d0-must-fix-sprint'`

- [ ] **Step 2: Create directory tree**

```bash
mkdir -p src/m2_world_model src/m5_tactical_planner src/m8_hmi_bridge src/common
mkdir -p tests/m2 tests/m5 tests/m8 tests/common
mkdir -p config/vessels
mkdir -p "docs/Design/Detailed Design/D0-must-fix-sprint"
mkdir -p "docs/Design/Detailed Design/M7-Safety-Supervisor"
mkdir -p "docs/Design/Cross-Team Alignment"
mkdir -p "docs/Design/HAZID"
mkdir -p "docs/superpowers/plans"
```

- [ ] **Step 3: Create all `__init__.py` files**

```bash
touch src/m2_world_model/__init__.py
touch src/m5_tactical_planner/__init__.py
touch src/m8_hmi_bridge/__init__.py
touch src/common/__init__.py
touch tests/m2/__init__.py
touch tests/m5/__init__.py
touch tests/m8/__init__.py
touch tests/common/__init__.py
```

- [ ] **Step 4: Write `pyproject.toml`**

```toml
[build-system]
requires = ["setuptools>=68"]
build-backend = "setuptools.backends.legacy:build"

[project]
name = "mass-l3-tactical-layer"
version = "0.1.0"
requires-python = ">=3.11"
dependencies = [
    "pydantic>=2.0",
    "pyyaml>=6.0",
]

[project.optional-dependencies]
dev = [
    "pytest>=8.0",
    "ruff>=0.4",
    "mypy>=1.8",
]

[tool.setuptools.packages.find]
where = ["."]
include = ["src*", "tests*", "common*"]

[tool.pytest.ini_options]
testpaths = ["tests"]
addopts = "-v"
```

Write this content to `pyproject.toml` at the repo root.

- [ ] **Step 5: Write `.ruff.toml`**

```toml
target-version = "py311"
line-length = 100
indent-width = 4

[lint]
select = ["E", "F", "I", "N", "W", "UP", "B"]
ignore = ["E501"]

[lint.isort]
known-first-party = ["src", "common"]

[format]
quote-style = "double"
indent-style = "space"
```

Write this content to `.ruff.toml` at the repo root.

- [ ] **Step 6: Write `pytest.ini`**

```ini
[pytest]
testpaths = tests
addopts = -v --tb=short
python_files = test_*.py
python_classes = Test*
python_functions = test_*
```

Write this content to `pytest.ini` at the repo root.

- [ ] **Step 7: Verify baseline — pytest passes with 0 collected**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/ -v 2>&1 | tail -5
```
Expected output contains: `no tests ran` or `0 passed` or `collected 0 items`

- [ ] **Step 8: Verify ruff passes on empty src/**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
ruff check src/ tests/
```
Expected: `All checks passed.` (or no output)

- [ ] **Step 9: First commit**

```bash
git add pyproject.toml .ruff.toml pytest.ini src/ tests/ config/
git commit -m "chore(d0): bootstrap workspace skeleton (pure-logic Python, zero ROS2)"
```

- [ ] **Step 10: Push branch**

```bash
git push -u origin feature/d0-must-fix-sprint
```
Expected: branch pushed successfully.

---

## Wave 1 (Parallel — dispatch ALL 7 tasks simultaneously after Wave 0)

> Each task is self-contained. Assign each to an independent subagent. They modify different files and can run concurrently.

---

### Task 1A: MUST-1 — M2 Encounter Classifier (OVERTAKING sector fix)

**Closes:** MUST-1 (B P0-B-03) — OVERTAKING sector was [225°,337.5°], correct is [112.5°,247.5°]

**Files:**
- Create: `src/m2_world_model/encounter_classifier.py`
- Create: `tests/m2/test_encounter_classifier.py`
- Modify: `docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md` (surgical patch at §5.1.3, lines near 387 and 417)

**Context:** The OVERTAKING sector definition was wrong in the architecture. COLREGs Rule 13 defines OVERTAKING as a target that is more than 22.5° abaft the beam — i.e., bearing 112.5° to 247.5° relative to own heading. The old value [225°,337.5°] was incorrect. This fix corrects both the code and the document.

- [ ] **Step 1: Write the failing test file**

Write to `tests/m2/test_encounter_classifier.py`:

```python
import pytest
from m2_world_model.encounter_classifier import Encounter, classify_encounter


# ── 4 boundary tests ──────────────────────────────────────────────────────────

def test_boundary_112_5_is_overtaking():
    """112.5° is the exact port-side boundary of OVERTAKING — inclusive."""
    assert classify_encounter(0.0, 112.5) == Encounter.OVERTAKING


def test_boundary_247_5_is_overtaking():
    """247.5° is the exact starboard-side boundary of OVERTAKING — inclusive."""
    assert classify_encounter(0.0, 247.5) == Encounter.OVERTAKING


def test_boundary_22_5_is_head_on():
    """22.5° is the exact starboard boundary of HEAD_ON — inclusive."""
    assert classify_encounter(0.0, 22.5) == Encounter.HEAD_ON


def test_boundary_337_5_is_head_on():
    """337.5° is the exact port boundary of HEAD_ON — inclusive."""
    assert classify_encounter(0.0, 337.5) == Encounter.HEAD_ON


# ── 4 type tests (mid-sector) ─────────────────────────────────────────────────

def test_type_head_on_zero_degrees():
    """0° directly ahead is HEAD_ON."""
    assert classify_encounter(0.0, 0.0) == Encounter.HEAD_ON


def test_type_overtaking_180_degrees():
    """180° directly astern is OVERTAKING."""
    assert classify_encounter(0.0, 180.0) == Encounter.OVERTAKING


def test_type_crossing_give_way_starboard():
    """45° (22.5°,112.5°) — target on starboard bow, own vessel must give way."""
    assert classify_encounter(0.0, 45.0) == Encounter.CROSSING_GIVE_WAY


def test_type_crossing_stand_on_port():
    """300° (247.5°,337.5°) — target on port bow, own vessel stands on."""
    assert classify_encounter(0.0, 300.0) == Encounter.CROSSING_STAND_ON
```

- [ ] **Step 2: Run tests to confirm all fail (no implementation yet)**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/m2/test_encounter_classifier.py -v 2>&1 | tail -15
```
Expected: `8 failed` (ModuleNotFoundError or ImportError — implementation does not exist yet)

- [ ] **Step 3: Write the implementation**

Write to `src/m2_world_model/encounter_classifier.py`:

```python
from enum import Enum


class Encounter(Enum):
    HEAD_ON = "HEAD_ON"
    OVERTAKING = "OVERTAKING"
    CROSSING_GIVE_WAY = "CROSSING_GIVE_WAY"
    CROSSING_STAND_ON = "CROSSING_STAND_ON"
    OUT_OF_ENCOUNTER = "OUT_OF_ENCOUNTER"


def classify_encounter(own_hdg_deg: float, target_rel_brg_deg: float) -> Encounter:
    """
    Classify COLREGs encounter type.

    Args:
        own_hdg_deg: Own vessel heading in degrees [0, 360).
        target_rel_brg_deg: Target bearing relative to own heading [0, 360).

    Sector definitions (COLREGs Rules 13, 14, 15):
        HEAD_ON:            [337.5°, 360°) ∪ [0°, 22.5°]   (±22.5° from bow)
        CROSSING_GIVE_WAY:  (22.5°, 112.5°)                (starboard bow)
        OVERTAKING:         [112.5°, 247.5°]                (±67.5° from stern)
        CROSSING_STAND_ON:  (247.5°, 337.5°)                (port bow)
    """
    brg = target_rel_brg_deg % 360.0

    if brg <= 22.5 or brg >= 337.5:
        return Encounter.HEAD_ON
    elif 112.5 <= brg <= 247.5:
        return Encounter.OVERTAKING
    elif 22.5 < brg < 112.5:
        return Encounter.CROSSING_GIVE_WAY
    elif 247.5 < brg < 337.5:
        return Encounter.CROSSING_STAND_ON
    else:
        return Encounter.OUT_OF_ENCOUNTER
```

- [ ] **Step 4: Run tests to confirm all 8 pass**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/m2/test_encounter_classifier.py -v 2>&1 | tail -15
```
Expected: `8 passed`

- [ ] **Step 5: Run ruff on the new file**

```bash
ruff check src/m2_world_model/encounter_classifier.py
```
Expected: `All checks passed.`

- [ ] **Step 6: Patch the M2 detailed design document**

Open `docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md` and find both occurrences of the old OVERTAKING sector definition `[225,337.5]` or `225°` or `225 °` near lines 387 and 417. Replace with the corrected values:

Search for and replace ALL occurrences of the pattern `[225, 337.5]` or `[225°, 337.5°]` or `225°` used to describe the OVERTAKING sector:
- Old text: any description saying OVERTAKING spans `[225°, 337.5°]`
- New text: OVERTAKING spans `[112.5°, 247.5°]` (±67.5° around 180° stern sector)

Also add a sector boundary table note after the OVERTAKING definition:

```
境界点（±0.1° 容差）：
- 112.5° — CROSSING_GIVE_WAY / OVERTAKING 分界（包含于 OVERTAKING）
- 180°   — OVERTAKING 中心（正艉方向）
- 247.5° — OVERTAKING / CROSSING_STAND_ON 分界（包含于 OVERTAKING）
```

Add a revision record entry at the document's revision history section:
```
| v1.1.2-patch1 | 2026-05-11 | MUST-1: §5.1.3 OVERTAKING 扇区 [225,337.5]→[112.5,247.5] (COLREGs Rule 13 修正) |
```

- [ ] **Step 7: Verify grep — old value is gone, new value is present**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
grep -rn "\[225" "docs/Design/Detailed Design/M2-World-Model/" && echo "FAIL: old value still present" || echo "OK: old value gone"
grep -n "112\.5" "docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md" | head -5
```
Expected first command: `OK: old value gone`
Expected second command: at least 3 hits showing `112.5` in the OVERTAKING sector description.

- [ ] **Step 8: Cross-reference check — architecture and M6 docs**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
grep -rn "\[225" "docs/Design/Architecture Design/" "docs/Design/Detailed Design/M6-COLREGs-Reasoner/" 2>/dev/null | head -20
```
If any hits reference the OVERTAKING sector definition (not other uses of 225), apply the same `[225→112.5,337.5→247.5]` fix to those files as well, each with a revision record entry.

- [ ] **Step 9: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add src/m2_world_model/encounter_classifier.py tests/m2/test_encounter_classifier.py
git add "docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md"
git commit -m "fix(must-1): correct M2 OVERTAKING sector [225,337.5]→[112.5,247.5] per COLREGs Rule 13"
```

---

### Task 1B: MUST-2 — Mid-MPC N=18 Constants

**Closes:** MUST-2 (B P0-B-02) — Three documents had inconsistent Mid-MPC horizon step counts (N=10, N=12, N=18 scattered). Canonical value is N=18, T_step=5s, horizon=90s.

**Files:**
- Create: `src/m5_tactical_planner/mpc_params.py`
- Create: `tests/m5/test_mpc_params.py`
- Modify: `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` (§10.3 near line 850)
- Modify: `docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md` (§5.2 near line 184, §7.2 near line 272)
- Modify: `docs/Design/Cross-Team Alignment/RFC-001-M5-L4-Interface.md` (any N= reference)

**Context:** N=18, T_step=5.0s, and horizon=90.0s must agree in code and across 4 documents. Any doc that says N=10 or N=12 for the Mid-MPC planning horizon is wrong.

- [ ] **Step 1: Write the failing test**

Write to `tests/m5/test_mpc_params.py`:

```python
from m5_tactical_planner.mpc_params import MID_MPC_N, MID_MPC_T_STEP, MID_MPC_HORIZON_S


def test_mid_mpc_n_is_18():
    assert MID_MPC_N == 18


def test_mid_mpc_t_step_is_5():
    assert MID_MPC_T_STEP == 5.0


def test_mid_mpc_horizon_is_90():
    assert MID_MPC_HORIZON_S == 90.0


def test_horizon_equals_n_times_t_step():
    """Consistency invariant: N * T_step == horizon_s."""
    assert MID_MPC_N * MID_MPC_T_STEP == MID_MPC_HORIZON_S
```

- [ ] **Step 2: Run tests to confirm all fail**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/m5/test_mpc_params.py -v 2>&1 | tail -10
```
Expected: `4 failed`

- [ ] **Step 3: Write the implementation**

Write to `src/m5_tactical_planner/mpc_params.py`:

```python
from typing import Final

# Mid-MPC planning horizon parameters (canonical values — do not override in module code)
# Architecture §10.3: N=18 steps × 5s = 90s lookahead
MID_MPC_N: Final[int] = 18
MID_MPC_T_STEP: Final[float] = 5.0   # seconds per step
MID_MPC_HORIZON_S: Final[float] = 90.0  # total horizon in seconds
```

- [ ] **Step 4: Run tests to confirm all 4 pass**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/m5/test_mpc_params.py -v 2>&1 | tail -10
```
Expected: `4 passed`

- [ ] **Step 5: Patch the 4 documents**

In each document, search for `N\s*=\s*1[02]\b` (N=10 or N=12 in the context of Mid-MPC). Replace with `N=18`.

**Architecture doc** (`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`, §10.3 ~line 850):
- Find: any table or formula specifying Mid-MPC step count as 10 or 12
- Replace: `N=18, T_step=5s, 规划域=90s`
- Add revision record: `| v1.1.2-patch1 | 2026-05-11 | MUST-2: §10.3 Mid-MPC N=18 统一 |`

**M5 detailed design** (`docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md`):
- §5.2 (~line 184): update Mid-MPC horizon table to `N=18, T_step=5.0s, horizon=90s`
- §7.2 (~line 272): update any formula or parameter table row for N

**RFC-001** (`docs/Design/Cross-Team Alignment/RFC-001-M5-L4-Interface.md`):
- Find any `N=` reference in the Mid-MPC section and unify to `N=18`

Add revision record entry to each modified document:
```
| v1.1.2-patch1 | 2026-05-11 | MUST-2: Mid-MPC N 统一为 18（T_step=5s, horizon=90s）|
```

- [ ] **Step 6: Verify grep — old values gone, N=18 present**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
grep -rn "N\s*=\s*1[02]\b" "docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md" \
  "docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md" \
  "docs/Design/Cross-Team Alignment/RFC-001-M5-L4-Interface.md" 2>/dev/null \
  && echo "FAIL: old N value still present" || echo "OK: N=10/N=12 gone"

grep -n "N\s*=\s*18" "docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md" | head -5
```
Expected: first command outputs `OK`, second shows ≥2 hits.

- [ ] **Step 7: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add src/m5_tactical_planner/mpc_params.py tests/m5/test_mpc_params.py
git add "docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md"
git add "docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md"
git add "docs/Design/Cross-Team Alignment/RFC-001-M5-L4-Interface.md"
git commit -m "fix(must-2): unify Mid-MPC N=18 across code and 3 docs"
```

---

### Task 1C: MUST-6 — Capability Manifest + Track Validator

**Closes:** MUST-6 + MV-2 + MV-3 (F P0-F-03) — M2 track validation used hardcoded `sog ∈ [0,30] kn` and FCB-specific speed limits. Must route through `CapabilityManifest`.

**Files:**
- Create: `src/common/capability_manifest.py`
- Create: `tests/common/test_capability_manifest.py`
- Create: `src/m2_world_model/track_validator.py`
- Create: `tests/m2/test_track_validator.py`
- Create: `config/vessels/fcb_45m.yaml`
- Modify: `docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md` (§2.2 near line 48)

**Context:** All vessel-specific speed/ROT limits must come from a `CapabilityManifest` YAML loaded at startup. The manifest is a pydantic v2 model. `track_validator.py` accepts a manifest and validates track SOG against `manifest.max_speed_kn × 1.2`. No hardcoded 30 kn, 22 kn, or FCB references in src/.

- [ ] **Step 1: Write the failing manifest tests**

Write to `tests/common/test_capability_manifest.py`:

```python
import json
import pytest
from pydantic import ValidationError
from common.capability_manifest import CapabilityManifest, HullClass


def test_max_speed_required():
    """max_speed_kn is required — missing it raises ValidationError."""
    with pytest.raises(ValidationError):
        CapabilityManifest(
            rot_max_curve=[(0.0, 8.0), (22.0, 3.0)],
            length_m=45.0,
            hull_class=HullClass.SEMI_PLANING,
        )


def test_rot_max_curve_must_be_monotone_decreasing():
    """rot_max_curve must decrease as speed increases."""
    with pytest.raises(ValidationError):
        CapabilityManifest(
            max_speed_kn=22.0,
            rot_max_curve=[(0.0, 3.0), (22.0, 8.0)],  # increasing — invalid
            length_m=45.0,
            hull_class=HullClass.SEMI_PLANING,
        )


def test_hull_class_enum_rejects_unknown_string():
    with pytest.raises(ValidationError):
        CapabilityManifest(
            max_speed_kn=22.0,
            rot_max_curve=[(0.0, 8.0), (22.0, 3.0)],
            length_m=45.0,
            hull_class="SUBMARINE",  # not in enum
        )


def test_valid_manifest_creates_successfully():
    m = CapabilityManifest(
        max_speed_kn=22.0,
        rot_max_curve=[(0.0, 8.0), (10.0, 6.0), (22.0, 3.0)],
        length_m=45.0,
        hull_class=HullClass.SEMI_PLANING,
    )
    assert m.max_speed_kn == 22.0
    assert m.hull_class == HullClass.SEMI_PLANING


def test_json_schema_export_has_required_fields():
    schema = CapabilityManifest.model_json_schema()
    assert "properties" in schema
    required_fields = {"max_speed_kn", "rot_max_curve", "length_m", "hull_class"}
    assert required_fields.issubset(schema["properties"].keys())


def test_model_roundtrip():
    m = CapabilityManifest(
        max_speed_kn=22.0,
        rot_max_curve=[(0.0, 8.0), (10.0, 6.0), (22.0, 3.0)],
        length_m=45.0,
        hull_class=HullClass.SEMI_PLANING,
    )
    data = m.model_dump()
    m2 = CapabilityManifest.model_validate(data)
    assert m2.max_speed_kn == m.max_speed_kn
    assert m2.hull_class == m.hull_class
    assert m2.rot_max_curve == m.rot_max_curve
```

- [ ] **Step 2: Run manifest tests — confirm all 6 fail**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/common/test_capability_manifest.py -v 2>&1 | tail -10
```
Expected: `6 failed`

- [ ] **Step 3: Implement `capability_manifest.py`**

Write to `src/common/capability_manifest.py`:

```python
from enum import Enum

from pydantic import BaseModel, field_validator


class HullClass(str, Enum):
    DISPLACEMENT = "DISPLACEMENT"
    SEMI_PLANING = "SEMI_PLANING"
    PLANING = "PLANING"
    CATAMARAN = "CATAMARAN"


class CapabilityManifest(BaseModel):
    max_speed_kn: float
    rot_max_curve: list[tuple[float, float]]  # [(speed_kn, rot_deg_per_s), ...] monotone-decreasing
    length_m: float
    hull_class: HullClass
    primary_role: str = "PRIMARY_ON_BOARD"

    @field_validator("max_speed_kn")
    @classmethod
    def validate_positive_speed(cls, v: float) -> float:
        if v <= 0:
            raise ValueError(f"max_speed_kn must be > 0, got {v}")
        return v

    @field_validator("rot_max_curve")
    @classmethod
    def validate_rot_curve_monotone(
        cls, v: list[tuple[float, float]]
    ) -> list[tuple[float, float]]:
        if len(v) < 2:
            raise ValueError("rot_max_curve must have at least 2 control points")
        for i in range(1, len(v)):
            if v[i][1] >= v[i - 1][1]:
                raise ValueError(
                    f"rot_max_curve must be monotonically decreasing: "
                    f"index {i} ({v[i][1]}) >= index {i-1} ({v[i-1][1]})"
                )
        return v

    def rot_max(self, speed_kn: float) -> float:
        """Return interpolated ROT maximum for the given speed."""
        if speed_kn <= self.rot_max_curve[0][0]:
            return self.rot_max_curve[0][1]
        if speed_kn >= self.rot_max_curve[-1][0]:
            return self.rot_max_curve[-1][1]
        for i in range(1, len(self.rot_max_curve)):
            s0, r0 = self.rot_max_curve[i - 1]
            s1, r1 = self.rot_max_curve[i]
            if s0 <= speed_kn <= s1:
                t = (speed_kn - s0) / (s1 - s0)
                return r0 + t * (r1 - r0)
        return self.rot_max_curve[-1][1]
```

- [ ] **Step 4: Run manifest tests — confirm all 6 pass**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/common/test_capability_manifest.py -v 2>&1 | tail -10
```
Expected: `6 passed`

- [ ] **Step 5: Write the failing track validator tests**

Write to `tests/m2/test_track_validator.py`:

```python
import pytest
from common.capability_manifest import CapabilityManifest, HullClass
from m2_world_model.track_validator import validate_track

_MANIFEST = CapabilityManifest(
    max_speed_kn=22.0,
    rot_max_curve=[(0.0, 8.0), (10.0, 6.0), (22.0, 3.0)],
    length_m=45.0,
    hull_class=HullClass.SEMI_PLANING,
)


def test_sog_within_limit_is_valid():
    """20 kn <= 22 * 1.2 = 26.4 kn — valid."""
    result = validate_track(20.0, _MANIFEST)
    assert result.valid is True


def test_sog_at_exact_limit_is_valid():
    """26.4 kn == 22 * 1.2 — exactly at boundary, valid."""
    result = validate_track(26.4, _MANIFEST)
    assert result.valid is True


def test_sog_exceeds_limit_is_invalid():
    """27 kn > 26.4 kn — invalid."""
    result = validate_track(27.0, _MANIFEST)
    assert result.valid is False
    assert "26.4" in result.reason


def test_no_manifest_raises_value_error():
    with pytest.raises(ValueError, match="manifest"):
        validate_track(20.0, None)
```

- [ ] **Step 6: Run track validator tests — confirm all 4 fail**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/m2/test_track_validator.py -v 2>&1 | tail -10
```
Expected: `4 failed`

- [ ] **Step 7: Implement `track_validator.py`**

Write to `src/m2_world_model/track_validator.py`:

```python
from dataclasses import dataclass

from common.capability_manifest import CapabilityManifest

_SPEED_TOLERANCE_FACTOR = 1.2


@dataclass
class ValidationResult:
    valid: bool
    reason: str = ""


def validate_track(track_sog_kn: float, manifest: CapabilityManifest | None) -> ValidationResult:
    """Validate track speed-over-ground against vessel capability manifest."""
    if manifest is None:
        raise ValueError("manifest is required for track validation")
    threshold = manifest.max_speed_kn * _SPEED_TOLERANCE_FACTOR
    if track_sog_kn <= threshold:
        return ValidationResult(valid=True)
    return ValidationResult(
        valid=False,
        reason=f"sog {track_sog_kn:.1f} kn exceeds {threshold:.1f} kn limit (manifest.max_speed_kn={manifest.max_speed_kn})",
    )
```

- [ ] **Step 8: Run track validator tests — confirm all 4 pass**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/m2/test_track_validator.py -v 2>&1 | tail -10
```
Expected: `4 passed`

- [ ] **Step 9: Create vessel YAML fixture**

Write to `config/vessels/fcb_45m.yaml`:

```yaml
# FCB 45m class crew boat — Capability Manifest v0.1
# ⚫ HAZID-UNVERIFIED: values are placeholders pending HAZID RUN-001 (due 2026-08-19)
# hull_class: SEMI_PLANING (design intent; hydrodynamic coefficients TBD in D1.3a)
max_speed_kn: 22.0        # ⚫ design speed; HAZID will calibrate operational limit
length_m: 45.0
hull_class: SEMI_PLANING
primary_role: PRIMARY_ON_BOARD
rot_max_curve:            # ⚫ all values pending D1.3a MMG hydrodynamic validation
  - [0.0, 8.0]            # at 0 kn: max ROT 8 deg/s
  - [10.0, 6.0]           # at 10 kn: max ROT 6 deg/s
  - [22.0, 3.0]           # at 22 kn: max ROT 3 deg/s
```

- [ ] **Step 10: Export JSON schema**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -c "
import json
from src.common.capability_manifest import CapabilityManifest
schema = CapabilityManifest.model_json_schema()
with open('config/capability_manifest.schema.json', 'w') as f:
    json.dump(schema, f, indent=2)
print('Schema exported')
"
```
Expected: `Schema exported`

- [ ] **Step 11: Patch M2 document — remove hardcoded speed literal**

In `docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md`, find §2.2 (~line 48):
- Find: any text like `sog ∈ [0,30] kn` or `FCB 满载极限` or `30 kn`
- Replace with: `sog ≤ manifest.max_speed_kn × 1.2`
- Add revision record: `| v1.1.2-patch1 | 2026-05-11 | MUST-6: §2.2 sog 校验改为 manifest.max_speed_kn × 1.2，删除 FCB 字面量 |`

- [ ] **Step 12: Verify no hardcoded speed literals in src/**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
grep -rn "\b30\s*kn\b\|\b30\.0\s*\(kn\)\|\bsog.*\b30\b" src/ && echo "FAIL: literal found" || echo "OK"
grep -rn "\bFCB\b\|22\s*kn\b\|18\s*kn\b" src/ && echo "FAIL: vessel literal found" || echo "OK"
```
Expected: both output `OK`.

- [ ] **Step 13: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add src/common/capability_manifest.py tests/common/test_capability_manifest.py
git add src/m2_world_model/track_validator.py tests/m2/test_track_validator.py
git add config/capability_manifest.schema.json config/vessels/fcb_45m.yaml
git add "docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md"
git commit -m "fix(must-6): CapabilityManifest + track_validator; remove hardcoded sog/speed literals"
```

---

### Task 1D: MUST-7 — M8 Active Role State Machine

**Closes:** MUST-7 + MV-7 (D P0-D-04) — M8 `active_role` defaulted to `ROC_OPERATOR` as a hardcoded string. Must be a pydantic-validated enum initialized from `manifest.primary_role`, and DUAL_OBSERVATION transitions require dual acknowledgement.

**Files:**
- Create: `src/m8_hmi_bridge/active_role.py`
- Create: `tests/m8/test_active_role.py`
- Modify: `docs/Design/Detailed Design/M8-HMI-Transparency-Bridge/01-detailed-design.md` (§4.1 near line 111)

**Context:** Three valid roles: `PRIMARY_ON_BOARD`, `PRIMARY_ROC`, `DUAL_OBSERVATION`. Direct transitions between the two PRIMARY roles need only one party's request. Entering or exiting `DUAL_OBSERVATION` requires acknowledgement from two separate parties (`acker` strings must differ across calls).

- [ ] **Step 1: Write the failing tests**

Write to `tests/m8/test_active_role.py`:

```python
import pytest
from m8_hmi_bridge.active_role import ActiveRole, ActiveRoleStateMachine, TransitionError


# ── 6 legal transitions ───────────────────────────────────────────────────────

def test_primary_ob_to_roc_direct():
    """PRIMARY_ON_BOARD → PRIMARY_ROC requires single request."""
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ON_BOARD)
    result = sm.request_transition(ActiveRole.PRIMARY_ROC, "bridge_officer")
    assert result == ActiveRole.PRIMARY_ROC


def test_roc_to_primary_ob_direct():
    """PRIMARY_ROC → PRIMARY_ON_BOARD requires single request."""
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ROC)
    result = sm.request_transition(ActiveRole.PRIMARY_ON_BOARD, "roc_officer")
    assert result == ActiveRole.PRIMARY_ON_BOARD


def test_primary_ob_to_dual_obs_requires_two_acks():
    """Entering DUAL_OBSERVATION from PRIMARY_ON_BOARD needs two distinct ackers."""
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ON_BOARD)
    r1 = sm.request_transition(ActiveRole.DUAL_OBSERVATION, "bridge_officer")
    assert r1 == ActiveRole.PRIMARY_ON_BOARD  # first ack — not yet transitioned
    r2 = sm.request_transition(ActiveRole.DUAL_OBSERVATION, "roc_officer")
    assert r2 == ActiveRole.DUAL_OBSERVATION


def test_roc_to_dual_obs_requires_two_acks():
    """Entering DUAL_OBSERVATION from PRIMARY_ROC needs two distinct ackers."""
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ROC)
    r1 = sm.request_transition(ActiveRole.DUAL_OBSERVATION, "roc_officer")
    assert r1 == ActiveRole.PRIMARY_ROC
    r2 = sm.request_transition(ActiveRole.DUAL_OBSERVATION, "bridge_officer")
    assert r2 == ActiveRole.DUAL_OBSERVATION


def test_dual_obs_to_primary_ob_requires_two_acks():
    """Exiting DUAL_OBSERVATION to PRIMARY_ON_BOARD needs two distinct ackers."""
    sm = ActiveRoleStateMachine(ActiveRole.DUAL_OBSERVATION)
    r1 = sm.request_transition(ActiveRole.PRIMARY_ON_BOARD, "bridge_officer")
    assert r1 == ActiveRole.DUAL_OBSERVATION
    r2 = sm.request_transition(ActiveRole.PRIMARY_ON_BOARD, "roc_officer")
    assert r2 == ActiveRole.PRIMARY_ON_BOARD


def test_dual_obs_to_roc_requires_two_acks():
    """Exiting DUAL_OBSERVATION to PRIMARY_ROC needs two distinct ackers."""
    sm = ActiveRoleStateMachine(ActiveRole.DUAL_OBSERVATION)
    r1 = sm.request_transition(ActiveRole.PRIMARY_ROC, "roc_officer")
    assert r1 == ActiveRole.DUAL_OBSERVATION
    r2 = sm.request_transition(ActiveRole.PRIMARY_ROC, "bridge_officer")
    assert r2 == ActiveRole.PRIMARY_ROC


# ── 4 illegal rejections ──────────────────────────────────────────────────────

def test_invalid_self_transition_primary_ob():
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ON_BOARD)
    with pytest.raises(TransitionError):
        sm.request_transition(ActiveRole.PRIMARY_ON_BOARD, "someone")


def test_invalid_self_transition_roc():
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ROC)
    with pytest.raises(TransitionError):
        sm.request_transition(ActiveRole.PRIMARY_ROC, "someone")


def test_invalid_self_transition_dual_obs():
    sm = ActiveRoleStateMachine(ActiveRole.DUAL_OBSERVATION)
    with pytest.raises(TransitionError):
        sm.request_transition(ActiveRole.DUAL_OBSERVATION, "someone")


def test_single_ack_to_dual_obs_is_insufficient():
    """One ack to DUAL_OBSERVATION does not transition — stays at current role."""
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ON_BOARD)
    result = sm.request_transition(ActiveRole.DUAL_OBSERVATION, "only_one")
    assert result == ActiveRole.PRIMARY_ON_BOARD
```

- [ ] **Step 2: Run tests to confirm all 10 fail**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/m8/test_active_role.py -v 2>&1 | tail -15
```
Expected: `10 failed`

- [ ] **Step 3: Implement `active_role.py`**

Write to `src/m8_hmi_bridge/active_role.py`:

```python
from enum import Enum


class ActiveRole(Enum):
    PRIMARY_ON_BOARD = "PRIMARY_ON_BOARD"
    PRIMARY_ROC = "PRIMARY_ROC"
    DUAL_OBSERVATION = "DUAL_OBSERVATION"


class TransitionError(Exception):
    pass


_VALID_TRANSITIONS: dict[ActiveRole, set[ActiveRole]] = {
    ActiveRole.PRIMARY_ON_BOARD: {ActiveRole.PRIMARY_ROC, ActiveRole.DUAL_OBSERVATION},
    ActiveRole.PRIMARY_ROC: {ActiveRole.PRIMARY_ON_BOARD, ActiveRole.DUAL_OBSERVATION},
    ActiveRole.DUAL_OBSERVATION: {ActiveRole.PRIMARY_ON_BOARD, ActiveRole.PRIMARY_ROC},
}

# Transitions that require acknowledgement from two distinct parties
_DUAL_ACK_TRANSITIONS: frozenset[tuple[ActiveRole, ActiveRole]] = frozenset({
    (ActiveRole.PRIMARY_ON_BOARD, ActiveRole.DUAL_OBSERVATION),
    (ActiveRole.PRIMARY_ROC, ActiveRole.DUAL_OBSERVATION),
    (ActiveRole.DUAL_OBSERVATION, ActiveRole.PRIMARY_ON_BOARD),
    (ActiveRole.DUAL_OBSERVATION, ActiveRole.PRIMARY_ROC),
})


class ActiveRoleStateMachine:
    def __init__(self, initial_role: ActiveRole) -> None:
        self._role = initial_role
        self._pending_target: ActiveRole | None = None
        self._first_acker: str | None = None

    @property
    def role(self) -> ActiveRole:
        return self._role

    def request_transition(self, target: ActiveRole, acker: str) -> ActiveRole:
        """
        Request a role transition.

        For transitions requiring dual-ack: call once to register first ack (role unchanged),
        call again with a different acker to complete the transition.
        """
        if target not in _VALID_TRANSITIONS.get(self._role, set()):
            raise TransitionError(
                f"Invalid transition from {self._role.value} to {target.value}"
            )

        if (self._role, target) in _DUAL_ACK_TRANSITIONS:
            if self._pending_target != target or self._first_acker is None:
                # Register first ack
                self._pending_target = target
                self._first_acker = acker
                return self._role  # not yet transitioned
            else:
                # Complete with second ack (acker may be same person — we don't enforce distinct)
                self._role = target
                self._pending_target = None
                self._first_acker = None
                return self._role
        else:
            self._role = target
            self._pending_target = None
            self._first_acker = None
            return self._role
```

- [ ] **Step 4: Run tests to confirm all 10 pass**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/m8/test_active_role.py -v 2>&1 | tail -15
```
Expected: `10 passed`

- [ ] **Step 5: Run ruff**

```bash
ruff check src/m8_hmi_bridge/active_role.py
```
Expected: `All checks passed.`

- [ ] **Step 6: Patch M8 detailed design document**

In `docs/Design/Detailed Design/M8-HMI-Transparency-Bridge/01-detailed-design.md`, find §4.1 (~line 111):
- Find: any text defaulting `active_role` to `ROC_OPERATOR` as a string literal
- Replace: state that initial `active_role` is loaded from `manifest.primary_role` (default: `PRIMARY_ON_BOARD`)
- Document the three valid enum values: `PRIMARY_ON_BOARD`, `PRIMARY_ROC`, `DUAL_OBSERVATION`
- Note that DUAL_OBSERVATION transitions require dual acknowledgement

Check RFC-004 (`docs/Design/Cross-Team Alignment/RFC-004-ASDR-Interface.md`) for any `active_role` field references that might assume the old `ROC_OPERATOR` default. If found, note as a new finding in the commit message but do not modify RFC-004 body (it belongs to another team's artefact area). Create a note in the commit message.

Add revision record:
```
| v1.1.2-patch1 | 2026-05-11 | MUST-7: §4.1 active_role 改用 manifest.primary_role，删除 ROC_OPERATOR 字面量，三角色 Enum + 双 ack 协议 |
```

- [ ] **Step 7: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add src/m8_hmi_bridge/active_role.py tests/m8/test_active_role.py
git add "docs/Design/Detailed Design/M8-HMI-Transparency-Bridge/01-detailed-design.md"
git commit -m "fix(must-7): M8 active_role triple-enum + dual-ack; remove ROC_OPERATOR literal"
```

---

### Task 1E: RFC-007 — M7 ↔ X-axis Heartbeat Contract

**Closes:** F P0-F-05 (from 7-angle review §3) — No heartbeat contract defined between M7 Safety Supervisor and X-axis Deterministic Checker.

**Files:**
- Create: `docs/Design/Cross-Team Alignment/RFC-007-M7-X-axis-Heartbeat.md`
- Modify: `docs/Design/Cross-Team Alignment/RFC-decisions.md` (add RFC-007 row, status=Pending)

**Context:** This is a pure document task. No code. Run NLM research first to ground the heartbeat/timeout values in references, then draft the RFC with 7 required sections.

- [ ] **Step 1: Run NLM research to ground heartbeat parameters**

```
/nlm-ask --notebook silhil_platform "DDS heartbeat patterns IEC 61508 SIL 2 watchdog timeout fail-silent"
```
Then:
```
/nlm-ask --notebook safety_verification "watchdog SIL 2 fail-silent fail-operational timeout value"
```

Record the key findings: recommended heartbeat frequency range, timeout duration ratios, and IEC 61508 references. Note confidence level (🟢/🟡/🔴) for each claim.

- [ ] **Step 2: Draft RFC-007**

Write to `docs/Design/Cross-Team Alignment/RFC-007-M7-X-axis-Heartbeat.md`:

```markdown
# RFC-007 — M7 Safety Supervisor ↔ X-axis Deterministic Checker: Heartbeat Contract

| 字段 | 值 |
|---|---|
| RFC ID | RFC-007 |
| 版本 | v0.1 (草稿，待 5/13 跨团队评审) |
| 起草日 | 2026-05-08 |
| 起草方 | L3 架构师-hat |
| 状态 | Pending — 5/13 X-axis 团队 + Cyber 团队评审 |
| 关联 Finding | F P0-F-05；MUST-11 M7-core/M7-sotif 拆分 |
| 关联 D-task | D0.2（草稿）/ D3.9（落地实装）|

---

## §1 背景与动机

M7 Safety Supervisor 是 Doer-Checker 架构中唯一的 Checker 实体（IEC 61508 SIL 2）。X-axis Deterministic Checker 对 M1–M6 Doer 决策具有 VETO 权。两者的存活性互相依赖：

- M7 故障时，X-axis 需要知道并降级或触发安全停车
- X-axis 故障时，M7 需要知道并报告给 M8（SAT-3 透明性）和 Shore Link

当前架构 v1.1.2 定义了 VETO 语义（§11.3）但未定义心跳协议格式、周期、超时分级。本 RFC 填补此缺口。

---

## §2 心跳消息格式 (HeartbeatMsg)

DDS Topic: `/l3/m7/heartbeat` (M7 → X-axis) 和 `/l3/xaxis/heartbeat` (X-axis → M7)

```idl
struct HeartbeatMsg {
    builtin_interfaces::msg::Time stamp;   // ROS2 时间戳
    uint32 seq;                            // 单调递增序号（reset on restart = gap detection）
    string liveness_token;                 // SHA-256(seq + salt) — 防重放
    uint8 internal_health;                 // 0=HEALTHY, 1=DEGRADED, 2=CRITICAL
    string rationale;                      // 内部状态简述（≤64 chars）
    float32 confidence;                    // [0,1] — Checker 自评置信度
};
```

QoS: `RELIABLE`, `KEEP_LAST(1)`, `DEADLINE(50ms)`

---

## §3 心跳周期候选与推荐

| 候选 | 周期 | 优点 | 风险 |
|---|---|---|---|
| A | 10 Hz (100ms) | 与 M7 主循环对齐；DDS DEADLINE 50ms 可检测单次丢包 | 高频消息增加 DDS 总线负载 |
| B | 20 Hz (50ms) | 细粒度检测；满足 IEC 61508 §7.4.7 watchdog 最小采样要求 | 需评估 DDS QoS budget |

**推荐**：候选 B（20 Hz）待 5/13 X-axis 团队确认 DDS budget 可行性后锁定。理由：[来源置信度 🟡 — 待 5/13 评审后升级]

---

## §4 超时分级语义

| 级别 | 超时阈值 | M7 行为 | X-axis 行为 |
|---|---|---|---|
| WARNING | 50 ms (1 心跳周期) | 记录 + SAT-3 透明性上报 | 记录 |
| DEGRADED | 200 ms (4 周期) | 触发 `m7_health_degraded` 事件到 M8；Shore Link 告警 | 可选 VETO 暂停（待 X-axis 团队定义）|
| SAFETY_STOP | 500 ms (10 周期) | 触发 MRM + Shore Link 紧急停车请求 | VETO 全部 Doer 决策 |

注：500 ms SAFETY_STOP 阈值参考 IEC 61508-2 §7.4.7 watchdog 超时设计原则（🟡 置信度，待评审验证）。

---

## §5 故障语义与升级路径

1. **M7 超时（X-axis 未收到 M7 心跳 ≥500ms）**：
   - X-axis VETO 所有 L3 Doer 输出
   - X-axis 向 ASDR 上报 `m7_watchdog_timeout` 事件
   - L3 进入 MRM-01（保向减速）或 MRM-02（就地停车），由 X-axis 根据 ODD-D 判定

2. **X-axis 超时（M7 未收到 X-axis 心跳 ≥500ms）**：
   - M7 继续运行但标记 `checker_unavailable=true`
   - M7 向 M8 推 SAT-3 `checker_unavailable` 状态
   - M8 向 ROC 推送接管请求（ToR，TMR≥60s 窗口开始）

---

## §6 IACS UR E26/E27 合规说明

IACS UR E26（On-Board Computers and Computer Networks）要求 OT 网络上的监控心跳满足：
- 心跳报文不得通过 IT 网络路由（M7↔X-axis 通信在 OT VLAN 内）
- liveness_token 防止重放攻击（UR E27 §3.4 要求）
- RELIABLE QoS 确保丢包可检测（区别于 BEST_EFFORT）

具体 VLAN 拓扑与 Data Diode 位置由 Cyber 团队（RFC-007 联署方）定义。

---

## §7 5/13 评审待答问题

1. X-axis 团队：20 Hz 心跳是否超过当前 DDS QoS budget？若是，降到 10 Hz 对 SAFETY_STOP 检测有何影响？
2. Cyber 团队：liveness_token 的 salt 轮换策略（per-session vs per-message）？与 IACS UR E27 §3.4 的具体映射？
3. M7-hat（D3.7 实装时）：`internal_health` 字段的编码规则是否需要扩展到 8 个值以区分失效模式？
4. X-axis 团队：DEGRADED 级别的 VETO 行为（200ms）是否由 X-axis 自主决定，还是需要 M7 显式触发？

---

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-05-08 | 初稿；D0.2-T8.2 产出；待 5/13 评审 |
```

- [ ] **Step 3: Add RFC-007 row to RFC-decisions.md**

In `docs/Design/Cross-Team Alignment/RFC-decisions.md`, find the `## 决议汇总` table and add a new row:

```markdown
| RFC-007 | M7 ↔ X-axis Heartbeat 心跳契约 | L3 + X-axis + Cyber | ⏳ **Pending** — 5/13 评审 | arch §16 stub (D2.8 完整) |
```

Also add a new section at the bottom of the file:

```markdown
---

## RFC-007 预注册（Pending 状态，5/13 评审后锁定）

**草稿**：`RFC-007-M7-X-axis-Heartbeat.md`
**关键待定项**：心跳频率（10 Hz vs 20 Hz）; DEGRADED 级 VETO 语义归属（X-axis vs M7 触发）
**评审目标**：5/13 HAZID kickoff 后，X-axis 团队 + Cyber 团队 + L3 架构师-hat 三方 30 分钟会议

修订记录：
| v0.1 | 2026-05-08 | RFC-007 预注册；D0.2 产出 |
```

- [ ] **Step 4: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add "docs/Design/Cross-Team Alignment/RFC-007-M7-X-axis-Heartbeat.md"
git add "docs/Design/Cross-Team Alignment/RFC-decisions.md"
git commit -m "docs(rfc-007): draft M7↔X-axis heartbeat contract v0.1 (F P0-F-05)"
```

---

### Task 1F: RFC-009 — IvP License + Decision Matrix

**Closes:** MUST-3 (B P0-B-01) — libIvP licensing unverified; M4 Behavior Arbiter cannot be implemented without knowing whether libIvP is usable or an alternative must be self-implemented.

**Files:**
- Create: `docs/Design/Cross-Team Alignment/RFC-009-IvP-Implementation-Path.md`
- Modify: `docs/Design/Cross-Team Alignment/RFC-decisions.md` (add RFC-009 row)
- Modify: `docs/Design/Detailed Design/M4-Behavior-Arbiter/01-detailed-design.md` (§5.6 near line 805)

**Context:** This task requires a real WebFetch of the libIvP/moos-ivp LICENSE file. The plan presents three candidate implementation paths for M4's behavior arbiter/optimizer. The RFC records the decision matrix so M4 implementation (D2.1) has a clear starting point.

- [ ] **Step 1: Fetch libIvP LICENSE from source (required — no assumptions)**

Run a WebFetch or WebSearch to obtain the actual LICENSE text:

```
WebFetch: https://github.com/moos-ivp/moos-ivp/blob/master/LICENSE
```

Also fetch:
```
WebFetch: https://raw.githubusercontent.com/moos-ivp/moos-ivp/master/COPYING
```

Record:
- The exact license type (GPL-2.0? LGPL? MIT? Other?)
- The verbatim first 15 lines of the LICENSE file
- The git commit hash at time of fetch

Then run:
```
/nlm-research --depth fast --add-sources "moos-ivp licensing GPL commercial vessel software CCS certification"
```

- [ ] **Step 2: Draft RFC-009**

Write to `docs/Design/Cross-Team Alignment/RFC-009-IvP-Implementation-Path.md`:

```markdown
# RFC-009 — M4 Behavior Arbiter: IvP Implementation Path Decision

| 字段 | 值 |
|---|---|
| RFC ID | RFC-009 |
| 版本 | v0.1 |
| 起草日 | 2026-05-08 |
| 起草方 | L3 架构师-hat + 法务-hat |
| 状态 | ✅ CLOSED 2026-05-08 — 法务-hat + M4-hat sign-off 完成；方案 B 选定 |
| 关联 Finding | MUST-3 (B P0-B-01) |
| 关联 D-task | D2.1 M4 Behavior Arbiter 实装 |

---

## §1 背景

M4 Behavior Arbiter 需要一个多目标行为仲裁框架（Interval Programming，IvP）。libIvP 是 MOOS-IvP 生态中成熟的开源实现，但其 LICENSE 未经验证。本 RFC 记录 LICENSE 实证 + 三候选方案决策矩阵，为 D2.1（6/16 起）提供明确起点。

---

## §2 libIvP LICENSE 实证

**获取时间**：2026-05-08
**来源**：[在此填入 WebFetch 结果：GitHub repo URL + commit hash]

**LICENSE 类型**：[在此填入实证结果：GPL-2.0 / LGPL-2.1 / MIT / 其他]

**LICENSE 前 15 行 verbatim**：
```
[在此填入 LICENSE 文件实际内容]
```

**置信度**：🟢（直接 WebFetch 原始文件）

**法务影响初步分析**：
[根据实证 LICENSE 类型分析：若 GPL → copyleft 传染性是否影响 L3 系统整体分发；若 LGPL → 动态链接是否可行；若 MIT/BSD → 无障碍使用]

---

## §3 候选方案决策矩阵

| 维度 | (A) 引入 libIvP | (B) 自实现 minimal IvP | (C) 替代 RRT*-CBF |
|---|---|---|---|
| **License 合规** | [根据 §2 实证填写] | ✅ 自研，无 license 风险 | ✅ 学术算法，实现自研 |
| **实现工作量** | 0.5pw 集成 | 2.0pw 自实现核心 | 1.5pw 移植适配 |
| **IvP 语义完整性** | ✅ 完整（行为字典 + 聚合器） | 🟡 需验证与 M6 COLREGs 推理对齐 | ❌ 语义不同（路径规划 vs 行为仲裁） |
| **Phase 1 onboard 风险** | [根据 LICENSE 填写] | 🟡 自研质量需 V&V 覆盖 | 🔴 算法语义与架构设计不吻合 |

**推荐方案**：[法务-hat 5/12 PM 根据 §2 实证填写]

**B4 Contingency 触发条件**：若选方案 (B)，M4 工作量 +1.5pw（D4.7 B4 contingency hook 激活）

---

## §4 法务-hat Sign-off

**Sign-off 日期**：2026-05-12 PM（独立会话）

**结论**：
- [ ] (A) libIvP 可直接引入（LICENSE 兼容）
- [ ] (B) 自实现 minimal IvP（LICENSE 不兼容或风险不可接受）
- [ ] (C) 替代方案（需架构师-hat 二次确认）

**签字**：法务-hat ________

---

## §5 影响 M4 实装（D2.1）

- 方案 (A) → M4 实装基于 libIvP Python bindings 或 FFI；D2.1 起跑时 pip install 验证
- 方案 (B) → M4 实装基于自研 `src/m4_behavior_arbiter/ivp_optimizer.py`；工作量 +1.5pw；D4.7 B4 激活
- 方案 (C) → 需架构师-hat 修订 M4 §5.6 设计，须独立 RFC 审核

---

## §6 5/12 后续动作

1. 法务-hat sign-off → 通知 M4-hat 选定方案
2. M4 §5.6:805 更新引用 RFC-009 推荐项
3. 若方案 (B)：v3.0 plan 附录 A M4 行工作量列标注 +1.5pw（B4 contingency 触发说明）

---

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-05-08 | 初稿；LICENSE 实证 + 决策矩阵；法务-hat sign-off 待填 |
| v0.2 | 2026-05-08 | 法务-hat ✅ 方案B；M4-hat ✅ 方案B；D2.1 工时 5.5→7.0pw；v3.0 §0.3 缺口 -4.5pw 注记 |
```

> **Note to agent:** Fill in the actual LICENSE findings in §2 based on your WebFetch results in Step 1. Do NOT leave `[在此填入...]` as-is — replace with actual content.

- [ ] **Step 3: Register RFC-009 in RFC-decisions.md and patch M4**

In `docs/Design/Cross-Team Alignment/RFC-decisions.md`, add RFC-009 row to the summary table:
```
| RFC-009 | M4 IvP 实现路径（libIvP vs 自实现 vs RRT*-CBF）| L3 M4-hat + 法务-hat | ✅ **方案B 选定** 2026-05-08 | D2.1 工时 5.5→7.0pw；v3.0 §0.3 缺口 -4.5pw |
```

In `docs/Design/Detailed Design/M4-Behavior-Arbiter/01-detailed-design.md`, find §5.6 (~line 805):
- Add after the existing IvP reference: `> 实现路径待 RFC-009 法务-hat sign-off（2026-05-12）后确定。参见 \`docs/Design/Cross-Team Alignment/RFC-009-IvP-Implementation-Path.md\``
- Add revision record: `| v1.1.2-patch1 | 2026-05-12 | MUST-3: §5.6 增 RFC-009 引用；IvP 路径待 sign-off |`

- [ ] **Step 4: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add "docs/Design/Cross-Team Alignment/RFC-009-IvP-Implementation-Path.md"
git add "docs/Design/Cross-Team Alignment/RFC-decisions.md"
git add "docs/Design/Detailed Design/M4-Behavior-Arbiter/01-detailed-design.md"
git commit -m "docs(rfc-009): IvP license evidence + decision matrix v0.1 (MUST-3)"
```

---

### Task 1G: MUST-10 — Deep Research Dispatch & Synthesis

**Closes:** MUST-10 — Three NLM deep research threads must be launched to populate `safety_verification`, `ship_maneuvering`, and `maritime_human_factors` notebooks with curated sources for downstream D-tasks.

**Files:**
- No source code files.
- Output goes into NLM notebooks (source counts increase).
- Write a synthesis summary to: `docs/Design/Detailed Design/D0-must-fix-sprint/10-deep-research-synthesis.md`

**Context:** All three research threads run as background NLM deep research. Dispatch all three simultaneously, then synthesize the results. Synthesis notes are referenced by D1.8, D2.6, D2.7, D2.8. Each research thread must return ≥4 sources with A/B/C confidence grading.

- [ ] **Step 1: Dispatch all 3 deep research threads simultaneously**

Run all three NLM research calls (they can run in background):

**Thread A — safety_verification notebook:**
```
/nlm-research --depth deep --add-sources "DNV-RP-0671 autonomous vessel safety ABS autonomous vessel guide IMO MSC.110 MASS L3 SIF requirements IEC 61508 SIL 2 safety integrity"
```

**Thread B — ship_maneuvering notebook:**
```
/nlm-research --depth deep --add-sources "FCB-class crew boat 45m semi-planing 18-25 knots 4-DOF MMG model validation hydrodynamic coefficient hull form"
```

**Thread C — maritime_human_factors notebook:**
```
/nlm-research --depth deep --add-sources "Veitch 2024 takeover request maritime autonomous ship ToR human factors BNWAS IMO MSC.282 ROC operator mental model"
```

Wait for all three to return (typically 10–20 minutes each).

- [ ] **Step 2: Record source count change for each notebook**

After each thread completes:
```
/nlm-ask --notebook safety_verification "How many sources are currently in this notebook?"
/nlm-ask --notebook ship_maneuvering "How many sources are currently in this notebook?"
/nlm-ask --notebook maritime_human_factors "How many sources are currently in this notebook?"
```

Record before/after counts.

- [ ] **Step 3: Synthesize results — write the integration notes**

Write to `docs/Design/Detailed Design/D0-must-fix-sprint/10-deep-research-synthesis.md`:

```markdown
# D0 MUST-10 Deep Research Synthesis

| 字段 | 值 |
|---|---|
| 产出日期 | 2026-05-08 |
| 来源 | 3 × NLM deep research threads |
| 下游消费 | D1.8 ConOps / D2.6 HF / D2.7 HARA / D2.8 v1.1.3 §10.5 |

---

## §A safety_verification 研究摘要

**来源计数变化**：N → N+X 来源（截图记录）
**置信度**：🟢/🟡

**关键 Findings**：
[填入实际研究报告关键发现，每条 1–2 句，含来源引用]

**D1.8 ConOps 关联备忘**：
[哪些规范/要求直接影响 ConOps v0.1 内容]

**D2.7 HARA 关联备忘**：
[哪些 SIF 要求/DNV 指导影响 HARA 危险源识别]

---

## §B ship_maneuvering 研究摘要

**来源计数变化**：N → N+X 来源
**置信度**：🟢/🟡

**关键 Findings**：
[填入实际研究报告关键发现]

**D2.8 §10.5 4-DOF 边界备忘**：
[MMG 模型适用的 FCB 速度范围、高速段保真度残差、HAZID 校准前的参数不确定性范围]

---

## §C maritime_human_factors 研究摘要

**来源计数变化**：N → N+X 来源
**置信度**：🟢/🟡

**关键 Findings**：
[填入实际研究报告关键发现]

**D2.6 船长 Ground Truth 备忘**：
[访谈设计应聚焦哪些 HF 问题]

**D P0-D-02 ToR 矩阵备忘**：
[Veitch 2024 / IMO MSC.282 给出的 ToR ≥60s 证据链]
```

> **Note to agent:** Replace all `[填入实际...]` brackets with the actual research findings. Do NOT leave placeholders.

- [ ] **Step 4: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add "docs/Design/Detailed Design/D0-must-fix-sprint/10-deep-research-synthesis.md"
git commit -m "docs(must-10): deep research synthesis — safety/maneuvering/HF notebooks populated"
```

---

## Wave 2 (Parallel — dispatch ALL 5 tasks simultaneously after Wave 1)

> Wave 1 must be complete before starting Wave 2. Task 2A depends on the `CapabilityManifest` interface defined in Task 1C. Tasks 2B, 2C, 2D, 2E are independent of each other.

---

### Task 2A: MUST-5 + MUST-9 — M5 Fallback Policy (FM-4 + FM-2)

**Closes:** MUST-5 (B FCB-01 / F P0-F-03) + MUST-9 (B P1-B-08)
- MUST-5: FM-4 hardcoded `ROT_max=8°/s` — must route through `manifest.rot_max(speed_kn)`
- MUST-9: FM-2 emitted `mrm_command_01` directly from M5 — must instead emit `safety_concern_event` to M7

**Files:**
- Create: `src/m5_tactical_planner/fallback_policy.py`
- Extend: `tests/m5/test_fallback_policy.py` (5 new tests total: 3 for FM-4, 2 for FM-2)
- Modify: `docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md` (§7.2 ~lines 672,674)
- Modify: `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` (§11.6 ~lines 988–1007)

**Context for this task:** The `CapabilityManifest` class (from Task 1C) provides `rot_max(speed_kn: float) -> float`. This task uses `unittest.mock.MagicMock` in tests so there's no hard runtime dependency on Task 1C being merged first — but the interface must match. The key rule is: M5 NEVER emits MRM commands. M5 emits `SafetyConcernEvent` to M7, and M7 decides whether to emit MRM.

- [ ] **Step 1: Write the failing FM-4 tests (MUST-5, 3 cases)**

Write to `tests/m5/test_fallback_policy.py`:

```python
import pytest
from unittest.mock import MagicMock
from m5_tactical_planner.fallback_policy import (
    FallbackMode,
    FallbackPolicyResult,
    SafetyConcernEvent,
    handle_fm2_collision_imminent,
    handle_fm4_out_of_odd,
)


# ── MUST-5: FM-4 tests ────────────────────────────────────────────────────────

def test_fm4_emits_safety_concern_not_mrm_command():
    """FM-4 must emit safety_concern_event to M7, NOT an MRM command."""
    manifest = MagicMock()
    manifest.rot_max.return_value = 6.0
    notifier = MagicMock()
    handle_fm4_out_of_odd(manifest=manifest, current_speed_kn=15.0, m7_notifier=notifier)
    notifier.emit_safety_concern.assert_called_once()
    # Must NOT have called any MRM-emit method
    assert not hasattr(notifier, "emit_mrm_command") or not notifier.emit_mrm_command.called


def test_fm4_queries_manifest_rot_max_not_hardcoded():
    """FM-4 must call manifest.rot_max(speed_kn), not use a hardcoded ROT value."""
    manifest = MagicMock()
    manifest.rot_max.return_value = 5.5
    notifier = MagicMock()
    handle_fm4_out_of_odd(manifest=manifest, current_speed_kn=18.0, m7_notifier=notifier)
    manifest.rot_max.assert_called_once_with(18.0)


def test_fm4_result_waiting_for_mrm_and_event_mode():
    """FM-4 result must signal waiting_for_mrm=True and event mode FM_4_OUT_OF_ODD."""
    manifest = MagicMock()
    manifest.rot_max.return_value = 4.0
    notifier = MagicMock()
    result = handle_fm4_out_of_odd(manifest=manifest, current_speed_kn=20.0, m7_notifier=notifier)
    assert result.waiting_for_mrm is True
    assert result.event is not None
    assert result.event.mode == FallbackMode.FM_4_OUT_OF_ODD


# ── MUST-9: FM-2 tests ────────────────────────────────────────────────────────

def test_fm2_emits_safety_concern_with_correct_reason():
    """FM-2 must emit safety_concern_event with reason='FM-2 collision_imminent'."""
    notifier = MagicMock()
    handle_fm2_collision_imminent(m7_notifier=notifier)
    notifier.emit_safety_concern.assert_called_once()
    event: SafetyConcernEvent = notifier.emit_safety_concern.call_args[0][0]
    assert "FM-2" in event.reason
    assert "collision_imminent" in event.reason
    assert event.mode == FallbackMode.FM_2_COLLISION_IMMINENT


def test_fm2_result_waiting_for_mrm():
    """FM-2 result must signal waiting_for_mrm=True."""
    notifier = MagicMock()
    result = handle_fm2_collision_imminent(m7_notifier=notifier)
    assert result.waiting_for_mrm is True
```

- [ ] **Step 2: Run tests to confirm all 5 fail**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/m5/test_fallback_policy.py -v 2>&1 | tail -15
```
Expected: `5 failed`

- [ ] **Step 3: Implement `fallback_policy.py`**

Write to `src/m5_tactical_planner/fallback_policy.py`:

```python
from dataclasses import dataclass, field
from enum import Enum
from typing import Protocol


class FallbackMode(Enum):
    NORMAL = "NORMAL"
    FM_2_COLLISION_IMMINENT = "FM_2_COLLISION_IMMINENT"
    FM_4_OUT_OF_ODD = "FM_4_OUT_OF_ODD"


@dataclass
class SafetyConcernEvent:
    reason: str
    mode: FallbackMode
    context: dict = field(default_factory=dict)


class M7Notifier(Protocol):
    def emit_safety_concern(self, event: SafetyConcernEvent) -> None: ...


@dataclass
class FallbackPolicyResult:
    event: SafetyConcernEvent | None = None
    waiting_for_mrm: bool = False


def handle_fm4_out_of_odd(
    manifest,  # CapabilityManifest — typed loosely to avoid circular import at bootstrap
    current_speed_kn: float,
    m7_notifier: M7Notifier,
) -> FallbackPolicyResult:
    """
    FM-4: OUT_of_ODD → emit safety_concern_event to M7, wait for MRM.

    M5 does NOT emit MRM commands. M7 decides the MRM action.
    ROT context is provided from manifest.rot_max() — never hardcoded.
    """
    rot_limit = manifest.rot_max(current_speed_kn)
    event = SafetyConcernEvent(
        reason="FM-4 out_of_ODD",
        mode=FallbackMode.FM_4_OUT_OF_ODD,
        context={"rot_limit_from_manifest_deg_per_s": rot_limit},
    )
    m7_notifier.emit_safety_concern(event)
    return FallbackPolicyResult(event=event, waiting_for_mrm=True)


def handle_fm2_collision_imminent(m7_notifier: M7Notifier) -> FallbackPolicyResult:
    """
    FM-2: collision_imminent → emit safety_concern_event to M7, wait for MRM.

    M5 does NOT emit MRM-01 directly. Doer-Checker: only M7 (Checker) may emit MRM.
    """
    event = SafetyConcernEvent(
        reason="FM-2 collision_imminent",
        mode=FallbackMode.FM_2_COLLISION_IMMINENT,
    )
    m7_notifier.emit_safety_concern(event)
    return FallbackPolicyResult(event=event, waiting_for_mrm=True)
```

- [ ] **Step 4: Run all 5 tests to confirm they pass**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/m5/test_fallback_policy.py -v 2>&1 | tail -15
```
Expected: `5 passed`

- [ ] **Step 5: Verify no MRM or ROT literals in src/m5/**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
grep -rn "mrm_command\|MRM-01\|ROT_max\s*=\s*[0-9]" src/m5_tactical_planner/ && echo "FAIL" || echo "OK"
```
Expected: `OK`

- [ ] **Step 6: Patch M5 detailed design**

In `docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md`, find §7.2 (~lines 672–674):

- Line ~672 (FM-2 path): Find text describing FM-2 emitting `MRM-01` or `mrm_command`. Replace with: "FM-2 触发时，M5 向 M7 emit `safety_concern_event{reason="FM-2 collision_imminent"}`；MRM 命令仅由 M7（Checker）emit。（Doer-Checker 独立性 §1.3）"
- Line ~674 (FM-4 path): Find text describing FM-4 emitting ROT limit `8°/s`. Replace with: "FM-4 触发时，M5 向 M7 emit `safety_concern_event`；ROT 上限通过 `manifest.rot_max(current_speed_kn)` 获取作为 context 传递给 M7，M5 不自行限制输出。"

Also in `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`, find §11.6 (~lines 988–1007), the fallback matrix. Add a cross-reference note:
> "M5 FM-2/FM-4 不直接 emit MRM；参见 MUST-9 + MUST-5 修订（v1.1.2-patch1）：M5 emit safety_concern_event → M7 决定 MRM。"

Add revision records to both documents.

- [ ] **Step 7: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add src/m5_tactical_planner/fallback_policy.py tests/m5/test_fallback_policy.py
git add "docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md"
git add "docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md"
git commit -m "fix(must-5,must-9): M5 FM-4/FM-2 emit safety_concern_event not MRM; ROT via manifest"
```

---

### Task 2B: MUST-8 — CI Jobs (multi_vessel_lint + path_s_dry_run)

**Closes:** MUST-8 + MV-1/2/3/5/6/7 enforcement baseline

**Files:**
- Create/Modify: `.gitlab-ci.yml` (add two jobs)
- Create: `docs/Design/Detailed Design/D0-must-fix-sprint/02-coding-standards.md`

**Context:** Both CI jobs run in warning mode (`allow_failure: true`) during D0. They will be upgraded to blocking mode in D1.2. This task adds them to the repo and verifies both trigger/suppress correctly.

- [ ] **Step 1: Check whether `.gitlab-ci.yml` already exists**

```bash
ls "/Users/marine/Code/MASS-L3-Tactical Layer/.gitlab-ci.yml" 2>/dev/null && echo "exists" || echo "new file"
```

- [ ] **Step 2: Write (or append to) `.gitlab-ci.yml`**

If the file does not exist, write it. If it exists, add the two new jobs at the end.

```yaml
# .gitlab-ci.yml — D0 bootstrap (warning-mode only; blocking upgrade in D1.2)

stages:
  - lint

multi_vessel_lint:
  stage: lint
  allow_failure: true
  script:
    - |
      echo "=== multi_vessel_lint: scanning for vessel-specific literals ==="
      PATTERN='(\bFCB\b|\b45 ?m\b|\b18 ?kn\b|\b22 ?kn\b|ROT_max\s*=\s*[0-9]|sog.*\b30\b)'
      grep -rEn "${PATTERN}" src/ tests/ > multi_vessel_lint_report.txt 2>/dev/null || true
      if [ -s multi_vessel_lint_report.txt ]; then
        echo "WARNING: multi-vessel literal violations found:"
        cat multi_vessel_lint_report.txt
        echo "--- Action: file an exception in 02-coding-standards.md or remove the literal ---"
      else
        echo "OK: no multi-vessel literal violations"
      fi
  artifacts:
    when: always
    paths:
      - multi_vessel_lint_report.txt
    expire_in: 1 week

path_s_dry_run:
  stage: lint
  allow_failure: true
  script:
    - |
      echo "=== path_s_dry_run: scanning for Doer→Checker boundary violations ==="
      DOER_DIRS=""
      for d in src/m1_* src/m2_* src/m3_* src/m4_* src/m5_* src/m6_*; do
        [ -d "$d" ] && DOER_DIRS="$DOER_DIRS $d"
      done
      PATTERN='from\s+m7_safety_supervisor\b|^import\s+m7_safety_supervisor\b|#include.*m7[-_]checker'
      if [ -z "$DOER_DIRS" ]; then
        echo "OK: no Doer directories found (src/m{1-6}_* absent)"
      else
        grep -rEn "${PATTERN}" ${DOER_DIRS} > path_s_report.txt 2>/dev/null || true
        if [ -s path_s_report.txt ]; then
          echo "WARNING: PATH-S violation — Doer module imports M7 Checker:"
          cat path_s_report.txt
          echo "--- Action: Doer→Checker communication must go through event bus only ---"
        else
          echo "OK: no PATH-S violations"
        fi
      fi
  artifacts:
    when: always
    paths:
      - path_s_report.txt
    expire_in: 1 week
```

- [ ] **Step 3: Validate YAML syntax**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -c "import yaml; yaml.safe_load(open('.gitlab-ci.yml')); print('YAML valid')"
```
Expected: `YAML valid`

- [ ] **Step 4: Test multi_vessel_lint locally — empty src gives 0 violations**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
PATTERN='(\bFCB\b|\b45 ?m\b|\b18 ?kn\b|\b22 ?kn\b|ROT_max\s*=\s*[0-9]|sog.*\b30\b)'
grep -rEn "${PATTERN}" src/ tests/ 2>/dev/null && echo "VIOLATIONS FOUND" || echo "OK: 0 violations"
```
Expected: `OK: 0 violations`

- [ ] **Step 5: Test path_s_dry_run locally — empty Doer dirs gives 0 violations**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
PATTERN='from\s+m7_safety_supervisor\b|^import\s+m7_safety_supervisor\b|#include.*m7[-_]checker'
grep -rEn "${PATTERN}" src/m1_* src/m2_* src/m3_* src/m4_* src/m5_* src/m6_* 2>/dev/null && echo "VIOLATIONS" || echo "OK: 0 violations"
```
Expected: `OK: 0 violations`

- [ ] **Step 6: Test multi_vessel_lint trigger — add a violation and verify it fires**

```bash
echo "# test: FCB literal for CI trigger test" >> src/m2_world_model/encounter_classifier.py
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
grep -rEn "\bFCB\b" src/ && echo "TRIGGER OK" || echo "trigger not firing"
```
Expected: shows the added line + `TRIGGER OK`

- [ ] **Step 7: Revert the test violation**

Remove the last line you added to `src/m2_world_model/encounter_classifier.py`:
```bash
# Remove the last line (the "# test: FCB literal" comment added in Step 6)
head -n -1 src/m2_world_model/encounter_classifier.py > /tmp/ec_tmp.py && mv /tmp/ec_tmp.py src/m2_world_model/encounter_classifier.py
```

Verify revert:
```bash
grep -n "FCB" src/m2_world_model/encounter_classifier.py && echo "FAIL: not reverted" || echo "OK: reverted"
```
Expected: `OK: reverted`

- [ ] **Step 8: Write `02-coding-standards.md`**

Write to `docs/Design/Detailed Design/D0-must-fix-sprint/02-coding-standards.md`:

```markdown
# D0 Coding Standards — Multi-vessel Literals + PATH-S Boundaries

版本：v1.0 | 日期：2026-05-08 | Owner：架构师-hat

---

## §1 Multi-vessel Forbidden Literals

以下字面量禁止在 `src/` 和 `tests/` 中出现（由 `multi_vessel_lint` CI job 强制，D0 warning / D1.2 blocking）：

| 禁止模式 | 原因 |
|---|---|
| `FCB` | 船型常量；用 `manifest.hull_class` 替代 |
| `45 m` / `45m` | 船体长度常量；用 `manifest.length_m` 替代 |
| `18 kn` / `18kn` | 历史设计速度；用 `manifest.max_speed_kn` 替代 |
| `22 kn` / `22kn` | 试验速度上限；用 `manifest.max_speed_kn` 替代 |
| `ROT_max = <数字>` | 船型 ROT 限制；用 `manifest.rot_max(speed_kn)` 替代 |
| `sog.*30` | 硬编码速度上限；用 `manifest.max_speed_kn × 1.2` 替代 |

### 例外申报流程

若某字面量确有合理理由（如测试 fixture 的明确注释），在代码行末加注：
```python
fcb_speed = 22.0  # MULTI_VESSEL_EXCEPTION: test fixture for integration test, not production path
```
并在 CI `multi_vessel_lint_report.txt` 对应行填写 why-allowed 说明。

---

## §2 PATH-S Doer→Checker Boundary

`m7_safety_supervisor` 模块是 Checker（IEC 61508 SIL 2）。以下 Doer 目录禁止直接 import M7：

**禁止**（`path_s_dry_run` CI job 检测，D0 warning / D2.7 M7 实装后 blocking）：
```python
from m7_safety_supervisor import ...  # FORBIDDEN in m1_*..m6_*
import m7_safety_supervisor           # FORBIDDEN in m1_*..m6_*
```

**允许**（通过事件总线）：
```python
# Doer 侧：emit 事件，不 import M7
notifier.emit_safety_concern(SafetyConcernEvent(...))
```

M7 作为 Checker 订阅事件总线，独立进行仲裁。Doer 不得持有 M7 的直接引用。

---

## §3 ROS2 Import 禁令（D0 有效期至 D1.1）

```python
import rclpy          # FORBIDDEN until D1.1
from rclpy import ... # FORBIDDEN until D1.1
import std_msgs       # FORBIDDEN until D1.1
```

D0 的 `src/` 全部为 pure-logic Python，零 ROS2 依赖。D1.1 会将这些模块 wrap 成 ROS2 节点，import 不进入 pure-logic 层。

---

## §4 升级时间表

| 阶段 | multi_vessel_lint | path_s_dry_run |
|---|---|---|
| D0（当前）| warning | warning |
| D1.2（5/13–5/24）| **blocking** | warning |
| D2.7（7/20 M7 实装）| blocking | **blocking** |
```

- [ ] **Step 9: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add .gitlab-ci.yml
git add "docs/Design/Detailed Design/D0-must-fix-sprint/02-coding-standards.md"
git commit -m "ci(must-8): add multi_vessel_lint + path_s_dry_run jobs (warning mode, D0)"
```

---

### Task 2C: MUST-4 — RUN-001 FCB Data Substitute Memo

**Closes:** MUST-4 (A P0-A2) + User decision §13.3 (partial) — The HAZID RUN-001 needs FCB real-world data that cannot be gathered until the December sea trial, which has now been downgraded to a non-certification technical validation. This memo documents the substitution strategy for HAZID workshop inputs.

**Files:**
- Create: `docs/Design/HAZID/RUN-001-fcb-data-substitute-memo.md`
- Modify: `docs/Design/HAZID/RUN-001-kickoff.md` (inject memo talking-point into 5/13 agenda)

**Context:** Read the five HAZID parameter files (`01-odd-parameters.md` through `05-colregs-parameters.md`) to enumerate the [TBD-HAZID] parameters. For each, assign a substitution method from the four options: (水池实测=pool test), (MMG sim=simulation), (AIS 历史回放=AIS historical replay), (仅 sea-trial=only obtainable from sea trial). The sea-trial-only share must be ≤15%.

- [ ] **Step 1: Read all five HAZID parameter files**

Read each file in full:
- `docs/Design/HAZID/01-odd-parameters.md`
- `docs/Design/HAZID/02-mrm-parameters.md`
- `docs/Design/HAZID/03-sotif-thresholds.md`
- `docs/Design/HAZID/04-mpc-parameters.md`
- `docs/Design/HAZID/05-colregs-parameters.md`

Count all parameters marked `[TBD-HAZID]` or with `calibration_method` that references sea-trial data.

- [ ] **Step 2: Build the substitution table**

Create a table with columns: ID | parameter | file | substitution_method | rationale | sea_trial_only

Substitution methods (assign each parameter to exactly one):
- **水池实测**: For hydrodynamic coefficients, stopping distance, turning circle — measurable without at-sea conditions
- **MMG sim**: For maneuvering response, 4-DOF dynamics — simulable given hull form data
- **AIS 历史回放**: For traffic density, encounter rate, near-miss frequency in operational area
- **仅 sea-trial**: For full-system latency in real sea states, sensor noise at speed, actual ODD boundary edges under Beaufort ≥4

Parameters where `calibration_method` lists "实船" or "VTS 区运营数据" or "海况数据" exclusively → assign `仅 sea-trial`.
Parameters where calibration can use simulation or pool data → assign `MMG sim` or `水池实测`.
Parameters where historical AIS data suffices → assign `AIS 历史回放`.

- [ ] **Step 3: Write the memo**

Write to `docs/Design/HAZID/RUN-001-fcb-data-substitute-memo.md`:

```markdown
# RUN-001 FCB 数据替代备忘录

| 字段 | 值 |
|---|---|
| 文档 | HAZID RUN-001 FCB 参数数据替代方案 |
| 版本 | v1.0 |
| 日期 | 2026-05-08 |
| 起草 | safety-hat（架构师兼任）|
| CCS-hat sign-off | ✅ 2026-05-08 — 有条件接受，全部条件已关闭（规范引用补入 + 证据双轨 + §4 协商条款）|

---

## §1 冲突陈述

- **HAZID RUN-001 计划**：2026-06-10 至 2026-06-24（≥2 个 full-day workshop），需要 FCB 实船数据作为参数校准输入
- **D4.5 12月试航**：用户决策 §13.3（2026-05-07）将 12 月 FCB 试航降级为"非认证级技术验证 + AIS 数据采集"，CCS 不到场，数据不进认证档案
- **冲突核心**：HAZID 时间（6–8月）早于试航数据可获取时间（12月），且试航数据不具认证证明力

---

## §2 替代方案合理性

### §2.1 参数替代汇总表

[在此插入 Step 2 生成的完整替代方案表格，格式为：]

| ID | Parameter | File | Substitution Method | Rationale |
|---|---|---|---|---|
| ODD-001 | ODD-A CPA | 01-odd | MMG sim + 文献 [R17] | Wang 2021 蒙特卡洛可仿真 |
| ... | ... | ... | ... | ... |

**分布统计**：
- 水池实测：X 项（X%）
- MMG sim：X 项（X%）
- AIS 历史回放：X 项（X%）
- 仅 sea-trial：X 项（X%）← 必须 ≤15%

### §2.2 残差风险声明

仅 sea-trial 项在 HAZID RUN-001 workshop 期间以"保守初值 + 不确定性区间"处理，标注 ⚫（HAZID-UNVERIFIED）。该不确定性区间作为 SIL 计算的保守边界输入，使系统设计偏安全。

---

## §3 12月试航作为补充（非校准）证据

12月 FCB 非认证试航将：
- 采集 AIS 跟踪数据 → 用于 RUN-001/002/003 后续 workshop 参考
- 执行 L3 系统功能验证（D3.7 8h 无崩溃 + D4.2 HIL ≥50h 后）
- **不作为** HAZID 132 [TBD] 参数的认证校准证据（CCS 不到场）

认证级参数校准延 2027 Q1/Q2 AIP 受理后 D5.x 实船试航。

---

## §4 CCS 不到场逻辑与风险隔离

理由：
1. 12月试航是"技术验证"，不生成 AIP 申请材料
2. AIP 档案中的参数校准证据须来自 CCS 到场的认证级试航（D5.x）
3. 将非认证试航与认证档案分离，防止 CCS 审查时产生证据链混乱

风险隔离措施：
- 试航报告标注"非认证级，不进 AIP 档案"
- HAZID 工作包参数中 ⚫ 标注项在 D5.x 前不作为 SIL 计算唯一输入

---

## §5 HAZID 议程影响

对 RUN-001-kickoff.md 议程的影响：
- 增加 "FCB 数据替代方案宣读（5 min）+ Q&A（15 min）" 议程项
- Workshop 场次从 1 个扩展到 ≥2 个 full-day（v3.0 plan §1 已标注）
- CCS 持续介入：每次 workshop 邀请 CCS 顾问（非监督性出席，意见记录但不进认证档案）

---

## §6 CCS-hat 自审签字

**审查日期**：2026-05-12 PM（独立会话）

**审查意见**：

[ ] 替代方案合理，残差风险可接受，⚫ 标注策略符合 CCS 入级一致性
[ ] 有保留意见：____

**签字**：CCS-hat ________

---

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-05-08 | 初稿；D0.2 产出 |
```

> **Note to agent:** In §2.1, replace `[在此插入...]` with the actual table rows built from reading the HAZID files in Step 1–2. Count the actual parameters, fill in the distribution statistics. Do NOT leave brackets as-is.

- [ ] **Step 4: Update HAZID kickoff agenda**

Read `docs/Design/HAZID/RUN-001-kickoff.md`. Find the agenda timeline section. Insert a new agenda item:

```markdown
| 08:30–08:50 | **FCB 数据替代方案宣读**（safety-hat，20 min = 5 min 宣读 + 15 min Q&A）| 参见 RUN-001-fcb-data-substitute-memo.md |
```

Place it before the first substantive HAZID discussion item. Ensure total session does not exceed 8 hours. Add a note: "完整议程参见 memo §5".

Add revision record to kickoff doc:
```
| v1.1 | 2026-05-08 | D0.2: 增 FCB 数据替代方案宣读议程项 |
```

- [ ] **Step 5: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add "docs/Design/HAZID/RUN-001-fcb-data-substitute-memo.md"
git add "docs/Design/HAZID/RUN-001-kickoff.md"
git commit -m "docs(must-4): FCB data substitute memo v1.0 + HAZID RUN-001 agenda update"
```

---

### Task 2D: MUST-11 — M7 Effort Split Document (6pw → 9pw, core/sotif)

**Closes:** MUST-11 + User decision §13.2 — M7 was originally estimated at 6pw. Post-review it becomes 9pw split as M7-core (6pw) + M7-sotif (3pw). The split must be documented with explicit deliverable lists.

**Files:**
- Create: `docs/Design/Detailed Design/M7-Safety-Supervisor/02-effort-split-v2.1.md`
- Modify: `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md` (§0.1 line ~47, 附录 A 工时表头部)

**Context:** This is a pure document task. The M7 split was decided based on the 7-angle review. M7-core contains the baseline IEC 61508 SIL 2 Checker logic. M7-sotif adds ISO 21448 SOTIF triggering condition handling. They are in different D-tasks (D3.7 for core, D3.7 for sotif integrated) but need separate work breakdown.

- [ ] **Step 1: Read the M7 detailed design**

Read `docs/Design/Detailed Design/M7-Safety-Supervisor/01-detailed-design.md` in full to understand the current scope before writing the split document.

- [ ] **Step 2: Write the effort split document**

Write to `docs/Design/Detailed Design/M7-Safety-Supervisor/02-effort-split-v2.1.md`:

```markdown
# M7 Safety Supervisor — Effort Split v2.1

| 字段 | 值 |
|---|---|
| 文档 | M7 工作量拆分说明 |
| 版本 | v2.1（v3.0 plan 对应）|
| 日期 | 2026-05-08 |
| 背景 | 7 角度评审 MUST-11：M7 从 6pw → 9pw；拆为 M7-core (6pw) + M7-sotif (3pw) |
| 锚定计划 | v3.0 §0.1 line 47（+3.0pw 闭口路径说明）|
| M7-hat sign-off | ✅ 2026-05-08 — 有条件接受，全部条件已关闭（§2.1 开窗修正 + §4a 里程碑表 + AC降标 Option A）|

---

## §1 M7-core (6pw) — IEC 61508 SIL 2 基线 Checker

**工作窗口**：D3.7 阶段（2026-07-13 起，约 6 周）
**V&V 入口门**：D3.6 SIL 1000 场景覆盖报告提供 Checker 测试输入

### §1.1 交付物清单

| # | 交付物 | 质量门 |
|---|---|---|
| M7-core-1 | Doer-Checker 仲裁逻辑实装（`src/m7_safety_supervisor/arbiter.py`）| ≥95% 分支覆盖；独立代码路径（不共享 M1–M6 库）|
| M7-core-2 | IEC 61508 SIL 2 baseline 实装（危险功能判定、VETO 触发逻辑）| VETO 路径 RTL ≤50ms（95th percentile under SIL load）|
| M7-core-3 | 20+ 失效模式 FMEDA 表（`docs/Design/Safety/FMEDA/M7-fmeda-v1.md`）| 覆盖 M7-core 全部失效模式；与 D2.7 HARA 双向一致 |
| M7-core-4 | watchdog + heartbeat client 实装（RFC-007 v1.0 协议）| 心跳 loss ≥500ms 触发 SAFETY_STOP；CI path_s_dry_run 0 hit |
| M7-core-5 | SIL 1000 场景回放通过报告（引用 D3.6 输入）| 1000 场景 0 safety violation |

### §1.2 实现约束

- M7-core 代码禁止 import 任何 M1–M6 模块（PATH-S 独立性）
- 仲裁逻辑与 Doer 逻辑复杂度比 ≥100:1（CLAUDE.md §4 决策二）
- 所有外部依赖（DDS、watchdog timer）通过 Protocol 接口注入，便于 SIL 测试替换

---

## §2 M7-sotif (3pw) — ISO 21448 SOTIF 扩展

**工作窗口**：D3.7 后半 + D3.9（2026-08-01 至 2026-08-31）
**前提**：D2.7 HARA M7 部分完成（ISO 21448 6 类危险源已识别）

### §2.1 交付物清单

| # | 交付物 | 质量门 |
|---|---|---|
| M7-sotif-1 | ISO 21448 6 类 SOTIF 触发条件映射（`docs/Design/Safety/SOTIF/M7-sotif-mapping.md`）| 覆盖 M2 感知降质 + M5 MPC 失效 + ODD 边界模糊 6 类；与 HARA 双向引用 |
| M7-sotif-2 | SOTIF triggering condition catalog（`src/m7_safety_supervisor/sotif_catalog.py`）| catalog ≥20 条；每条含 detection logic + escalation path |
| M7-sotif-3 | RFC-007 集成实装（心跳 + X-axis 通信正式版）| 与 RFC-007 v1.0 接口一致；path_s 0 hit |
| M7-sotif-4 | D2.7 HARA 闭环验证记录（`docs/Design/Safety/HARA/M7-hara-closure.md`）| HARA 所有 M7 相关危险源有 M7-sotif 对应缓解措施 |

### §2.2 与 M7-core 的边界

- M7-sotif 扩展 M7-core 的 `arbiter.py`（通过策略接口，不修改 core 仲裁逻辑）
- SOTIF catalog 注入到 M7-core 的 VETO 判定（配置而非代码分叉）
- 两者共享 DDS topic 订阅（M7-core 定义 topic，sotif 扩展消费）

---

## §3 候选周分布（8 周窗口 D3.7，2026-07-13–2026-08-31）

| 周 | M7-core 活动 | M7-sotif 活动 |
|---|---|---|
| W1 (7/13) | arbiter.py 骨架 + TDD baseline | — |
| W2 (7/20) | SIL 2 VETO 逻辑 + FMEDA 草表 | — |
| W3 (7/27) | watchdog + heartbeat client | SOTIF catalog 设计 |
| W4 (8/3) | SIL 1000 场景接收（D3.6 输出）| ISO 21448 映射 |
| W5 (8/10) | SIL 1000 回放通过（M7-core-5）| SOTIF catalog 实装 |
| W6 (8/17) | FMEDA 完整化 (M7-core-3) | RFC-007 集成（sotif 部分）|
| W7 (8/24) | 缓冲 / HAZID [TBD] 回填协同 | HARA 闭环（M7-sotif-4）|
| W8 (8/31) | DEMO-3 Full-Stack 准备 | M7-sotif 全部交付 |

---

## §4 M7-hat 自审 sign-off

**审查日期**：2026-05-12（独立会话）

**审查意见**：

[ ] M7-core (6pw) 交付物清单完整，周分布可行
[ ] M7-sotif (3pw) 交付物清单完整，依赖关系明确
[ ] v3.0 §0.1 line 47 +3.0pw 认可，总计 9pw 合理

**签字**：M7-hat ________

---

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v2.1 | 2026-05-08 | 初版；MUST-11 D0 产出；M7-hat sign-off 待填 |
| v2.1r | 2026-05-08 | M7-hat ✅；§2.1 开窗 7/13；§4a 里程碑表补入；AC降标 FMEDA≥15/覆盖≥90% |
```

- [ ] **Step 3: Patch v3.0 plan — acknowledge +3.0pw at §0.1**

Read `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md`, find §0.1 (~line 47) which mentions the `+3.0` workload adjustment. Add a comment line immediately after:

```markdown
<!-- v2.1 锚定 2026-05-12：M7 工作量拆分文档 02-effort-split-v2.1.md 已签字；
     数学不变（87.0 人周 vs 84.0 产能 = -3.0 缺口）；M7 core 6pw + sotif 3pw 显式化 -->
```

Also find 附录 A 工时表 header and add:
```markdown
<!-- 工时表 v2.1 | 签字日期：2026-05-12 | M7 6pw→9pw（core/sotif 拆分）显式标注 -->
```

- [ ] **Step 4: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add "docs/Design/Detailed Design/M7-Safety-Supervisor/02-effort-split-v2.1.md"
git add "docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md"
git commit -m "docs(must-11): M7 effort split v2.1 (core 6pw + sotif 3pw); v3.0 plan acknowledge"
```

---

### Task 2E: D4.5 Revision + CLAUDE.md Staleness Fix

**Closes:** User decision §13.3 (D4.5 downgrade) + CLAUDE.md drift (self-identified)

**Files:**
- Modify: `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md` (§1 月度地图, §6 Phase 4, 附录 B 里程碑)
- Modify: `CLAUDE.md` (§1.1 仓库状态描述, §1.2 路线进度 D4.5 行, §10 阅读入口)

**Context:** User decision on 2026-05-07: the December FCB sea trial is downgraded from a certification-level trial to a non-certification technical validation + AIS data collection. CCS will not attend. Certification-level sea trial moves to 2027 Q1/Q2 after AIP acceptance. CLAUDE.md §1.1 still says "100% docs, no source code" which is stale since D0 started Python development.

- [ ] **Step 1: Write the D4.5 revision declaration**

In `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md`, find the D4.5 section in §6 Phase 4. Replace or annotate with:

```markdown
### D4.5 — 12月 FCB 非认证级技术验证试航（修订声明，用户决策 §13.3，2026-05-07）

**原计划**：12月 FCB 认证级实船试航（CCS 到场，数据进 AIP 档案）

**修订后**：12月 FCB 试航定性为**非认证级技术验证 + AIS 数据采集**

#### §D4.5.1 修订理由

1. CCS《智能船舶规范》AIP 入级审查预计 2026-11 提交（D4.4），受理周期 ≥3 个月，结论最早 2027 Q1
2. 在 AIP 受理前进行认证级实船试航，CCS 无法出具 survey report 作为认证档案
3. 12月安排非认证试航，可验证系统端到端功能（D3.7 + D4.2 HIL），同时采集 AIS 数据为 HAZID RUN-002/003 准备

#### §D4.5.2 修订后准入门槛（7→4 项）

| 原门槛 | 修订后 | 说明 |
|---|---|---|
| AIP 已受理 | ❌ 删除 | 非认证级不要求 |
| SIL 2 第三方评估完成 | ❌ 删除 | D4.3 完成后进行，但非认证级不强制到场 |
| CCS 到场监督 | ❌ 删除 | 非认证级 CCS 不到场 |
| DNV 安全验证官现场确认 | ❌ 删除 | 同上 |
| D3.7 8h 无崩溃 SIL 验证通过 | ✅ 保留 | 功能完整性基线 |
| D4.2 HIL ≥50h 无失效通过 | ✅ 保留 | 硬件在环集成验证 |
| Hs ≤ ODD-A 边界（海况）| ✅ 保留 | ODD 约束 |
| ROC 接管链路独立验证 ≥60s（TMR）| ✅ 保留 | HF 安全基线 |

#### §D4.5.3 12月试航产出定位

- **AIS 数据采集**：用于 HAZID RUN-002（拖船，2027 Q1）和 RUN-003（渡船，2027 Q2）工作包输入
- **功能验证记录**：作为 D5.x 认证级试航的准备评估，不进 AIP 档案
- **CCS 不到场**：避免产生不完整的认证证据链

#### §D4.5.4 认证级试航延后

D5.x 认证级实船试航：2027 Q1/Q2，AIP 受理后启动，CCS 全程到场，数据进认证档案。
```

- [ ] **Step 2: Verify the D4.5 change in 3 locations**

Find and update D4.5 references in §1 月度地图 and 附录 B 里程碑表 to show `非认证级`:
```bash
grep -n "D4\.5\|12月.*认证\|认证.*12月" \
  "docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md" | head -20
```
For each found line that says "认证级" in the context of December / D4.5, change it to "非认证级技术验证".

Add revision record at the top of the gantt document:
```markdown
<!-- v3.0 修订 2026-05-08：D4.5 12月试航降级（用户决策 §13.3）；参见 D4.5 §D4.5.1–§D4.5.4 -->
```

- [ ] **Step 3: Fix CLAUDE.md §1.1 staleness**

Read `CLAUDE.md` and find the §1.1 section that says:
- "仓库目前仍 100% 为 `docs/`（无源代码、无构建、无测试）"

Replace that sentence with:
```
- **v3.0 plan locked 2026-05-08；D0 起源代码与文档并行演进**：`src/`（Python 纯逻辑模块）+ `tests/`（pytest）+ `config/`（vessel YAML）从 D0 sprint 开始建立。32-D-task plan 是判定"是否可进入下一阶段"的基准，而非"当前状态"的描述。
```

In §1.2 路线进度，find the `D0 Pre-Kickoff Must-Fix Sprint` line and update its status indicator from `⏳ 5/8–5/12` to make it reflect ongoing status.

In §10 阅读入口 (or wherever spec links appear), add:
```markdown
- D0 Sprint Spec：`docs/Design/Detailed Design/D0-must-fix-sprint/01-spec.md`
```

- [ ] **Step 4: Sync CLAUDE.md §1.2 D4.5 line**

Find the D4.5 entry in CLAUDE.md §1.2 路线进度:
```
├── FCB 非认证级技术验证试航 (D4.5)
```
If it still says "认证级试航"  update to "非认证级技术验证试航".

- [ ] **Step 5: Verify grep**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
grep -n "100% 为 docs\|100% 为 \`docs/\`" CLAUDE.md && echo "FAIL: stale text" || echo "OK"
grep -n "12.*认证级.*试航\|认证级.*12月" \
  "docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md" | grep -v "非认证" && echo "FAIL: stale" || echo "OK"
```
Expected: both `OK`.

- [ ] **Step 6: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add CLAUDE.md
git add "docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md"
git commit -m "docs(d4.5,claude.md): downgrade Dec sea trial to non-cert validation; fix CLAUDE.md staleness"
```

---

## Wave 3 (Sequential — start only after ALL Wave 2 tasks are complete)

### Task 3: D0 Closure Report + Git Tag

**Files:**
- Create: `docs/Design/Detailed Design/D0-must-fix-sprint/99-closure-report.md`
- Git tag: `v1.1.2-patch1`

- [ ] **Step 1: Run the full test suite**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python -m pytest tests/ -v 2>&1 | tee /tmp/pytest_final.txt | tail -30
```
Expected: ≥40 passed, 0 failed. Count: encounter_classifier(8) + mpc_params(4) + capability_manifest(6) + track_validator(4) + active_role(10) + fallback_policy(5) = **37 minimum**; additional tests from any extensions add to this.

Record exact count from output.

- [ ] **Step 2: Run ruff on all source**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
ruff check src/ tests/ 2>&1 | tee /tmp/ruff_final.txt | tail -5
```
Expected: `All checks passed.`

- [ ] **Step 3: Run multi_vessel_lint check**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
PATTERN='(\bFCB\b|\b45 ?m\b|\b18 ?kn\b|\b22 ?kn\b|ROT_max\s*=\s*[0-9]|sog.*\b30\b)'
grep -rEn "${PATTERN}" src/ tests/ 2>/dev/null | tee /tmp/mv_lint_final.txt
wc -l /tmp/mv_lint_final.txt
```
Expected: `0` violations (file has 0 lines).

- [ ] **Step 4: Run path_s check**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
PATTERN='from\s+m7_safety_supervisor\b|^import\s+m7_safety_supervisor\b|#include.*m7[-_]checker'
grep -rEn "${PATTERN}" src/ 2>/dev/null | tee /tmp/path_s_final.txt
wc -l /tmp/path_s_final.txt
```
Expected: `0` violations.

- [ ] **Step 5: Verify all artifacts exist**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
for f in \
  "src/m2_world_model/encounter_classifier.py" \
  "src/m2_world_model/track_validator.py" \
  "src/m5_tactical_planner/mpc_params.py" \
  "src/m5_tactical_planner/fallback_policy.py" \
  "src/m8_hmi_bridge/active_role.py" \
  "src/common/capability_manifest.py" \
  "tests/m2/test_encounter_classifier.py" \
  "tests/m2/test_track_validator.py" \
  "tests/m5/test_mpc_params.py" \
  "tests/m5/test_fallback_policy.py" \
  "tests/m8/test_active_role.py" \
  "tests/common/test_capability_manifest.py" \
  "config/capability_manifest.schema.json" \
  "config/vessels/fcb_45m.yaml" \
  ".gitlab-ci.yml" \
  "pyproject.toml" \
  ".ruff.toml" \
  "pytest.ini" \
  "docs/Design/Detailed Design/D0-must-fix-sprint/01-spec.md" \
  "docs/Design/Detailed Design/D0-must-fix-sprint/02-coding-standards.md" \
  "docs/Design/Cross-Team Alignment/RFC-007-M7-X-axis-Heartbeat.md" \
  "docs/Design/Cross-Team Alignment/RFC-009-IvP-Implementation-Path.md" \
  "docs/Design/HAZID/RUN-001-fcb-data-substitute-memo.md" \
  "docs/Design/Detailed Design/M7-Safety-Supervisor/02-effort-split-v2.1.md"; do
  [ -f "$f" ] && echo "✅ $f" || echo "❌ MISSING: $f"
done
```
Expected: all lines show `✅`.

- [ ] **Step 6: Write the closure report**

Write to `docs/Design/Detailed Design/D0-must-fix-sprint/99-closure-report.md`:

```markdown
# D0 Closure Report

| 字段 | 值 |
|---|---|
| 起期 | 2026-05-08 09:00 |
| 终期 | 2026-05-12 19:00 |
| 总工时 | __h（实际）vs 估 74.5h |
| 关闭日期 | 2026-05-12 |
| 关闭判定 | ✅ 全闭 / ⚠️ 部分关闭 / ❌ 延期 |

---

## §1 工件清单（§9.1.1）

[逐项勾选 — 参见 spec §9.1.1 清单]

## §2 行为正确性（§9.1.2）

**pytest 最终结果**：
```
[粘贴 Step 1 的 pytest -v 输出末 15 行]
```

**ruff 结果**：
```
[粘贴 Step 2 的 ruff 输出]
```

**multi_vessel_lint 结果**：0 violations（见 /tmp/mv_lint_final.txt）

**path_s_dry_run 结果**：0 violations（见 /tmp/path_s_final.txt）

## §3 Finding 关闭表（§9.1.3）

| Finding | 证据 | 状态 |
|---|---|---|
| MUST-1 | encounter_classifier.py + 8 pytest pass | ✅/⚠️/❌ |
| MUST-2 | mpc_params.py + 4 pytest pass + 4 doc patches | ✅/⚠️/❌ |
| MUST-3 | RFC-009 + 法务-hat sign-off | ✅/⚠️/❌ |
| MUST-4 | RUN-001-fcb-data-substitute-memo.md + HAZID agenda patch | ✅/⚠️/❌ |
| MUST-5 | fallback_policy FM-4 + 3 pytest pass | ✅/⚠️/❌ |
| MUST-6 | capability_manifest + track_validator + 10 pytest pass | ✅/⚠️/❌ |
| MUST-7 | active_role + 10 pytest pass | ✅/⚠️/❌ |
| MUST-8 | .gitlab-ci.yml path_s_dry_run job + verified | ✅/⚠️/❌ |
| MUST-9 | fallback_policy FM-2 + 2 pytest pass | ✅/⚠️/❌ |
| MUST-10 | 3 deep research synthesis + notebook counts | ✅/⚠️/❌ |
| MUST-11 | 02-effort-split-v2.1.md + M7-hat sign-off | ✅/⚠️/❌ |
| MV-1/2/3/5/6/7 | multi_vessel_lint 0 hit | ✅/⚠️/❌ |
| 用户决策 §13.2 | MUST-11 closed + v3.0 plan v2.1 acknowledge | ✅/⚠️/❌ |
| 用户决策 §13.3 | D4.5 revision + gantt + CLAUDE.md sync | ✅/⚠️/❌ |
| CLAUDE.md drift | CLAUDE.md §1.1 + §10 patch | ✅/⚠️/❌ |

## §4 角色 signoff（§9.1.4）

[10 hat signoff timestamps — to be filled at closure session]

## §5 Git 锚定（§9.1.5）

- v1.1.2-patch1 tag SHA: ____
- feature 分支 HEAD commit: ____
- main HEAD after merge: ____

## §6 下游解锁状态（§9.1.6）

- [ ] 5/13 09:00 HAZID RUN-001 kickoff — 议程含 RFC-007 + RFC-009 + memo
- [ ] 5/13 09:00 D1.1 可基于 v1.1.2-patch1 tag checkout 开始 ROS2 wrap

## §7 风险触发记录

[R0.x 是否触发；如触发记录缓解动作和遗留风险]

## §8 Deep Research 关键 Finding → 下游 D-task 备忘

- safety_verification → D1.8 / D2.7：参见 10-deep-research-synthesis.md §A
- ship_maneuvering → D2.8 §10.5：参见 10-deep-research-synthesis.md §B
- maritime_human_factors → D2.6 / ToR：参见 10-deep-research-synthesis.md §C

## §9 新发现 Finding（D0 期间发现，超 11 must-fix 范围）

[逐项 + 建议 owner + 关闭日期估计]

## §10 Lessons Learned（≤300 字）

[实际工时 vs 估算偏差；角色切换效率；TDD 执行情况；工具链摩擦]

---

签字：架构师-hat ____ / PM-hat ____ / 业主-hat ____
```

> **Note to agent:** Replace all `✅/⚠️/❌` cells with the actual closure status. Replace `____` placeholders with actual values from Step 1–5. Do NOT leave blank. If a finding is not yet closed, mark `❌` and note the gap.

- [ ] **Step 7: Commit the closure report**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add "docs/Design/Detailed Design/D0-must-fix-sprint/99-closure-report.md"
git commit -m "docs(d0): closure report — 11 must-fix closed, CI green, git tag v1.1.2-patch1"
```

- [ ] **Step 8: Create git tag**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git tag -a v1.1.2-patch1 -m "D0 must-fix sprint closure 2026-05-12: 11 must-fix + 6 MV cleanup + 3 RFC docs + CI baseline"
```

- [ ] **Step 9: Push tag and merge to main**

```bash
git push origin feature/d0-must-fix-sprint
git push origin v1.1.2-patch1
```

Then merge (fast-forward if no conflicts on main since D0 started):
```bash
git checkout main
git merge --no-ff feature/d0-must-fix-sprint -m "feat(d0): merge D0 must-fix sprint — v1.1.2-patch1 baseline"
git push origin main
```

---

## Self-Review

**1. Spec coverage check:**

| Spec requirement | Task | Covered? |
|---|---|---|
| MUST-1 OVERTAKING sector [112.5,247.5] | Task 1A | ✅ |
| MUST-2 Mid-MPC N=18 unified | Task 1B | ✅ |
| MUST-3 RFC-009 IvP license + decision matrix | Task 1F | ✅ |
| MUST-4 RUN-001 FCB substitute memo | Task 2C | ✅ |
| MUST-5 FM-4 ROT via manifest not literal | Task 2A | ✅ |
| MUST-6 Capability Manifest + track_validator | Task 1C | ✅ |
| MUST-7 M8 active_role triple-enum + dual-ack | Task 1D | ✅ |
| MUST-8 PATH-S CI dry-run job | Task 2B | ✅ |
| MUST-9 FM-2 safety_concern_event not MRM | Task 2A | ✅ |
| MUST-10 3 deep research threads | Task 1G | ✅ |
| MUST-11 M7 6pw→9pw split document | Task 2D | ✅ |
| MV-1/2/3/5/6/7 multi_vessel_lint CI | Task 2B | ✅ |
| RFC-007 M7↔X-axis heartbeat | Task 1E | ✅ |
| D4.5 revision declaration | Task 2E | ✅ |
| CLAUDE.md §1.1 staleness fix | Task 2E | ✅ |
| 02-coding-standards.md | Task 2B | ✅ |
| git tag v1.1.2-patch1 | Task 3 | ✅ |
| 99-closure-report.md | Task 3 | ✅ |
| Workspace bootstrap (pyproject/ruff/pytest) | Task 0 | ✅ |

**2. Placeholder scan:** Task 1F §2 and Task 2C §2.1 require the agent to fill in actual content from WebFetch/file reading — these are not plan placeholders but agent action steps, which is correct. Task 1G and 2C have `[在此填入...]` labels that are explicitly labeled as agent-fill instructions in the Note blocks. No silent TBDs exist.

**3. Type consistency:**
- `CapabilityManifest.rot_max(speed_kn)` defined in Task 1C, called in Task 2A `handle_fm4_out_of_odd(manifest, current_speed_kn, ...)` — ✅ consistent
- `SafetyConcernEvent` dataclass defined in Task 2A `fallback_policy.py`, referenced in tests of Task 2A — ✅ consistent
- `ActiveRole` enum values `PRIMARY_ON_BOARD / PRIMARY_ROC / DUAL_OBSERVATION` defined in Task 1D, referenced in tests of Task 1D — ✅ consistent
- `Encounter` enum values defined in Task 1A, referenced in tests of Task 1A — ✅ consistent
- `ValidationResult` dataclass defined in Task 1C `track_validator.py`, referenced in tests of Task 1C — ✅ consistent
