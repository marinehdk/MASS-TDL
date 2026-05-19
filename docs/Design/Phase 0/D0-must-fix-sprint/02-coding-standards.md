# D0 Coding Standards — Multi-vessel Lint + PATH-S Boundary

| Field | Value |
|---|---|
| Document | D0 coding standards |
| Sprint | D0 Pre-Kickoff Must-Fix (2026-05-08 to 2026-05-12) |
| Status | v1.0 |

---

## §1 Multi-vessel Prohibition List

These patterns are forbidden in `src/m{1..7}_*/` (Doer modules) and enforced by `multi_vessel_lint` CI job:

| Pattern | Reason | Exception path |
|---|---|---|
| `\bFCB\b` | Vessel name literal | `config/vessels/*.yaml` only |
| `45\s*m` | FCB hull length | `config/vessels/*.yaml` only |
| `18\s*kn` | Vessel speed constant | `config/vessels/*.yaml` only |
| `22\s*kn` | Vessel speed constant | `config/vessels/*.yaml` only |
| `ROT_max\s*=\s*[0-9]` | Hardcoded ROT limit | Use `manifest.rot_max(speed_kn)` instead |
| `sog\s*<=?\s*30` | Hardcoded speed ceiling | Use `manifest.max_speed_kn * 1.2` instead |

Vessel-specific parameters belong in `config/vessels/<vessel_id>.yaml` and are accessed via `CapabilityManifest` (`src/common/capability_manifest.py`).

### Exception Declaration Process

If a literal must appear in `src/` for a specific justified reason, add a comment on the same line:

```python
# multi_vessel_lint: allow — <reason> (approved by <hat> on <date>)
```

The CI report will still flag it, but the explanation will be in-file.

---

## §2 PATH-S Doer-Checker Boundary

Modules M1–M6 (Doers) must NEVER import M7 Safety Supervisor (Checker):

```python
# FORBIDDEN in src/m{1,2,3,4,5,6}_*/:
from m7_safety_supervisor import ...
import m7_safety_supervisor
```

Enforced by `path_s_dry_run` CI job (warning mode in D0; becomes blocking at D2.7 M7 implementation start).

**Communication direction**: M1–M6 emit `safety_concern_event` messages; M7 subscribes and decides MRM. This is enforced at the architecture level (publish-subscribe, not direct import).

### Background (from CLAUDE.md §4, Decision #2)

The Doer-Checker pattern is a topological safety principle:
- **M1–M6** produce candidate decisions (the Doer path)
- **M7** independently validates those decisions (the Checker path, must be **100× simpler** in logic and implementation)
- M7's implementation must **not share code, libraries, or data structures** with M1–M6

Direct imports would violate this independence and fail certification audit (IEC 61508 SIL 2 requirement).

---

## §3 CI Jobs Summary

| Job | Stage | Mode | Artifact | Blocking |
|---|---|---|---|---|
| `multi_vessel_lint` | lint | warning (`allow_failure: true`) | `multi_vessel_lint_report.txt` | No (D0); Yes after D1.2 |
| `path_s_dry_run` | lint | warning (`allow_failure: true`) | `path_s_report.txt` | No (D0); Yes after D2.7 |
| `pytest` | unit-test | blocking | test results | Yes |
| `ruff_check` (stage-1-lint) | lint | blocking | — | Yes (D1.2) |

**Timeline**:
- **D0 (5/8–5/12)**: Both jobs run in **warning mode**; failures do not block merge
- **D1.2 (on/before 6/15 DEMO-1)**: `multi_vessel_lint` becomes **blocking**
- **D2.7 (on/before 7/31 DEMO-2)**: `path_s_dry_run` becomes **blocking**

---

## §4 Quick Reference: Accessing Vessel Parameters

### Wrong (Forbidden)

```python
# ❌ Hardcoded literals
if vessel_name == "FCB":
    max_speed = 18  # kn

if sog > 22:  # kn
    ...
```

### Correct

```python
# ✅ Via CapabilityManifest
from common.capability_manifest import CapabilityManifest

manifest = CapabilityManifest.load_from_yaml("config/vessels/fcb.yaml")
max_speed = manifest.max_speed_kn
rot_max = manifest.rot_max(current_speed_kn=18.5)

if sog > manifest.max_speed_kn * 1.1:  # Apply safety margin
    ...
```

---

## §5 Verification

Both jobs output **text artifact reports** visible in GitLab CI logs and downloadable from the pipeline artifacts page.

To run locally before commit:

```bash
cd ~/Code/MASS-L3-Tactical Layer

# Test multi_vessel_lint
grep -rn --include="*.py" -E "(\bFCB\b|45\s*m\b|18\s*kn\b|22\s*kn\b|ROT_max\s*=\s*[0-9]|sog\s*<=?\s*30)" src/ || echo "OK: 0 violations"

# Test path_s_dry_run
grep -rn --include="*.py" -E "(from\s+m7_safety_supervisor|import\s+m7_safety_supervisor)" src/ 2>/dev/null || echo "OK: 0 violations"
```

Both should report "OK: 0 violations" before submission.

---

## References

- Architecture Report v1.1.2, §2 (Decision #2: Doer-Checker dual-path)
- Architecture Report v1.1.2, §4 (Decision #4: Multi-ship via Capability Manifest)
- CLAUDE.md, §4 (Topmost-level architecture constraints)
