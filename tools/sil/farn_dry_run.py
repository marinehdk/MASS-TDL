"""farn dry-run: sample strategies and preview case counts without folder generation."""
import itertools
import json
import sys
from importlib.metadata import version
from pathlib import Path

import farn
from farn.sampling.discrete import DiscreteSampling

FARN_VERSION = version("farn")
BASE = Path(__file__).resolve().parent
LOG_FILE = BASE / "farn_dry_run.log"

FIXED_FACTORS = {
    "rule": ["Rule14", "Rule15", "Rule13"],
    "odd_zone": ["A", "B", "C"],
    "disturbance_level": [0, 1, 2, 3, 4],
    "seed": [1, 2, 3, 4, 5],
}

CONTINUOUS_PARAMS = {
    "target_bearing_deg": [0.0, 360.0],
    "target_sog_kn": [5.0, 20.0],
    "wind_kn": [0.0, 30.0],
    "current_kn": [0.0, 5.0],
}

LHS_SAMPLES = 500
SOBOL_SAMPLES = 500


def run_fixed_grid():
    print("\n--- Strategy: fixed_grid ---")
    names = list(FIXED_FACTORS.keys())
    values = list(FIXED_FACTORS.values())
    product = list(itertools.product(*values))
    count = len(product)
    print(f"  type:       full factorial")
    print(f"  factors:    {names}")
    print(f"  levels:     {[len(v) for v in values]}")
    print(f"  case count: {count}")
    print(f"  expected:   225 {'MATCH' if count == 225 else 'MISMATCH'}")
    print(f"  preview:")
    for combo in product[:5]:
        case_name = "_".join(str(v) for v in combo)
        print(f"    {case_name}")
    return count


def run_lhs(stype: str, name_label: str, num_samples: int) -> int:
    print(f"\n--- Strategy: {name_label} ---")
    params = {"_names": list(CONTINUOUS_PARAMS.keys()),
              "_numberOfSamples": num_samples}
    if stype == "sobol":
        params["_onset"] = 0
    ranges = []
    for key in params["_names"]:
        lo, hi = CONTINUOUS_PARAMS[key]
        params[f"_ranges_{key}"] = [lo, hi]
        ranges.append([lo, hi])
    params["_ranges"] = ranges

    ds = DiscreteSampling()
    ds.set_sampling_type(stype)
    ds.set_sampling_parameters(params, name_label)
    samples = ds.generate_samples()
    case_names = samples.get("_case_name", [])
    count = len(case_names)
    print(f"  type:       {stype}")
    print(f"  params:     {params['_names']}")
    print(f"  ranges:     {ranges}")
    print(f"  case count: {count}")
    print(f"  expected:   {num_samples} {'MATCH' if count == num_samples else 'MISMATCH'}")
    print(f"  preview:")
    for cn in case_names[:5]:
        print(f"    {cn}")
    return count


def main():
    print(f"FARN DRY-RUN  1100-cell case folder preview")
    print(f"farn version: {FARN_VERSION}")

    c1 = run_fixed_grid()
    c2 = run_lhs("uniformLhs", "lhs", LHS_SAMPLES)
    c3 = run_lhs("sobol", "sobol", SOBOL_SAMPLES)
    total = c1 + c2 + c3

    print(f"\n--- Summary ---")
    print(f"  fixed_grid: {c1}")
    print(f"  lhs:        {c2}")
    print(f"  sobol:      {c3}")
    print(f"  Total:      {total} (expected 1225)")
    print(f"  {'-- MATCH --' if total == 1225 else '-- MISMATCH --'}")
    return 0 if total == 1225 else 1


if __name__ == "__main__":
    log_f = open(LOG_FILE, "w")
    orig_stdout = sys.stdout
    class Tee:
        def write(self, buf):
            orig_stdout.write(buf)
            log_f.write(buf)
        def flush(self):
            orig_stdout.flush()
            log_f.flush()
    sys.stdout = Tee()
    try:
        sys.exit(main())
    finally:
        sys.stdout = orig_stdout
        log_f.close()
