#!/usr/bin/env python3
"""Apply IEC 61508-3 classifications from D3.5 spec §3.2 and §3.3 to param_catalog_v1.csv.

Run once from repo root after A-T1. Updates iec61508_class and aou_range in-place.
"""
import csv
import re
from pathlib import Path

CSV_PATH = Path(
    "docs/Design/Phase 3/D3.5-arch-hazid-backfill/evidence/param_catalog_v1.csv"
)

INDEPENDENT: list[tuple[str, str, str]] = [
    ("T_standOn",        "6-12 min (ODD-A) / 3-6 min (ODD-B)",   "Wang 2021 + Imazu 1987"),
    ("T_act",            "4-9 min (ODD-A)",                       "Wang 2021 (T_standOn x 0.75)"),
    ("Rule_19",          "fixed (COLREG Rule 19(b))",              "COLREG Rule 19(b) literal"),
    ("Rule_8",           "20-45 deg",                              "COLREGs Rule 8 + MMG"),
    ("MPC.*N",           "fixed (RFC-001 locked N=18/90s)",       "RFC-001 cross-team locked"),
    ("BC.*MPC.*k",       "5-11",                                   "Loe 2008 VO + Monte Carlo"),
    ("Checker",          "10-35%/100 cycles",                      "ADR-001 Doer-Checker 1:100"),
    ("AIS",              "1.5sigma-3sigma / 7.5-30 s",            "Standard fusion CI"),
    ("YAML_m_x",        "+-15% (nonlinear perturbation)",         "Yasukawa 2015 [R7]"),
    ("YAML_m_y",        "+-15%",                                   "Yasukawa 2015 [R7]"),
    ("YAML_J_zz",       "+-15%",                                   "Yasukawa 2015 [R7]"),
    ("YAML_X_",         "+-20%",                                   "Yasukawa 2015 hull derivatives"),
    ("YAML_Y_",         "+-20%",                                   "Yasukawa 2015 hull derivatives"),
    ("YAML_N_",         "+-20%",                                   "Yasukawa 2015 hull derivatives"),
    ("YAML_G_M",        "1.0-2.5 m",                              "Inclining experiment range"),
    ("YAML_T_phi",      "3-8 s",                                   "FCB roll period estimate"),
    ("YAML_t_P",        "+-10%",                                   "Yasukawa 2015 propeller"),
    ("YAML_w_P",        "+-10%",                                   "Yasukawa 2015 propeller"),
    ("YAML_D_P",        "fixed (design value)",                    "FCB design drawing"),
    ("YAML_k_",         "+-15%",                                   "Yasukawa 2015 KT coefficients"),
    ("YAML_t_R",        "+-10%",                                   "Yasukawa 2015 rudder"),
    ("YAML_a_H",        "+-10%",                                   "Yasukawa 2015 rudder"),
    ("YAML_x_H",        "+-5%",                                    "Yasukawa 2015 rudder"),
    ("YAML_x_R",        "fixed (hull geometry)",                   "FCB design drawing"),
    ("YAML_gamma_R",    "+-10%",                                   "Yasukawa 2015 rudder"),
    ("YAML_l_R",        "+-5%",                                    "Yasukawa 2015 rudder"),
    ("YAML_kappa",      "+-10%",                                   "Yasukawa 2015 wake factor"),
    ("YAML_epsilon",    "+-10%",                                   "Yasukawa 2015 flow factor"),
    ("YAML_A_R",        "fixed (design value)",                    "FCB design drawing"),
    ("YAML_f_alpha",    "+-10%",                                   "Yasukawa 2015 rudder lift"),
    ("YAML_u0",         "+-2 kn",                                  "FCB typical cruise speed range"),
]

CONFIGURATION_OR_DEPENDENT: list[tuple[str, str, str]] = [
    ("IvP",                "Configuration",           "Changes IvP solver arbitration space dimension"),
    ("Conformance.*w_E",   "Dependent calibration",   "Joint constraint w_E+w_T+w_H=1"),
    ("Conformance.*w_T",   "Dependent calibration",   "Joint constraint w_E+w_T+w_H=1"),
    ("Conformance.*w_H",   "Dependent calibration",   "Joint constraint w_E+w_T+w_H=1"),
    ("CPA.*TCPA",          "Dependent calibration",   "Multi-threshold joint ODD sub-domain classification"),
    ("ToR",                "Dependent calibration",   "Joint constraint TMR window 16 cells"),
    ("YAML_L",             "Independent calibration",  "FCB scale, HAZID field measurement"),
    ("YAML_d",             "Independent calibration",  "FCB draft, HAZID field measurement"),
    ("YAML_B",             "Independent calibration",  "FCB beam, HAZID field measurement"),
    ("YAML_displacement",  "Independent calibration",  "FCB displacement, HAZID field measurement"),
    ("YAML_x_G",           "Independent calibration",  "FCB CG, HAZID field measurement"),
]


def apply_classifications(rows: list[dict]) -> list[dict]:
    updated = 0
    for row in rows:
        pid = row['param_id']

        for pattern, cls, _ in CONFIGURATION_OR_DEPENDENT:
            if re.search(pattern, pid, re.IGNORECASE):
                if row['iec61508_class'] == '[manual review]':
                    row['iec61508_class'] = cls
                    updated += 1
                break
        else:
            for pattern, aou, _ in INDEPENDENT:
                if re.search(pattern, pid, re.IGNORECASE):
                    if row['iec61508_class'] == '[manual review]':
                        row['iec61508_class'] = 'Independent calibration'
                        if not row['aou_range']:
                            row['aou_range'] = aou
                        updated += 1
                    break

    print(f"Applied classifications to {updated} rows")
    remaining = sum(1 for r in rows if r['iec61508_class'] == '[manual review]')
    print(f"Still [manual review]: {remaining} rows")
    return rows


def main() -> None:
    rows = list(csv.DictReader(open(CSV_PATH, encoding='utf-8')))
    rows = apply_classifications(rows)

    with open(CSV_PATH, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    print(f"Updated {CSV_PATH}")


if __name__ == '__main__':
    main()
