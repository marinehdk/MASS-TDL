# HAZID RUN-001 Output Format Agreement

**Status:** Draft (pending safety engineer confirmation)
**Date:** 2026-05-25
**Safety engineer:** [TBD - 外包安全工程师，5/15-7/10 在岗]

## Output format: JSON (preferred) or CSV

## Required fields

| HAZID field | D3.5 CSV field | Notes |
|---|---|---|
| param_id or param_name | param_id | Must match param_catalog_v1.csv param_id or a mappable name |
| hazid_calibrated_value | hazid_calibrated_value | Numeric calibrated value |
| uncertainty_pct | uncertainty_pct | ± percentage uncertainty (or absolute ± bounds) |
| applicable_odd | applicable_odd | Applicable ODD sub-domains: ODD-A, ODD-B, ODD-C, ODD-D, or ALL |
| calibration_method | calibration_method | Brief description of calibration method used |
| confidence | (metadata) | High / Medium / Low |

## Delivery path

HAZID RUN-001 output file to be placed at:
`docs/Design/Phase 3/D3.5-arch-hazid-backfill/evidence/hazid_run001_output.json`

Or emailed to architecture team lead for manual placement.

## Delivery date

2026-08-19 (HAZID RUN-001 completion target per master plan)

## Validation rules

1. Every param_id in param_catalog_v1.csv with `iec61508_class != Independent calibration` must have a corresponding HAZID entry
2. `hazid_calibrated_value` must be numeric (no text values)
3. `uncertainty_pct` must be a positive number or range string (e.g., "5" or "3-8")
4. `applicable_odd` must be one of: ODD-A, ODD-B, ODD-C, ODD-D, ALL, or comma-separated combination
5. Missing entries will be flagged as WARNING during B-T1 parsing; uncovered params retain `[HAZID-UNVERIFIED]` annotation

## Coordination notes

- Safety engineer on-site period: 2026-05-15 to 2026-07-10
- Format confirmation deadline: 2026-07-31 (Track A close)
- HAZID RUN-001 target completion: 2026-08-19
- If format changes after confirmation, update this document and notify D3.5 owner
