#!/usr/bin/env python3
"""D3.5 parameter extraction script.

Produces param_catalog_v1.csv from three sources (in dedup priority order):
  1. Appendix E tables (E.1-E.5) - structured, highest fidelity
  2. fcb_dynamics.yaml HAZID-UNVERIFIED fields - MMG hydrodynamic params
  3. Architecture doc body inline markers - TBD-HAZID / 初始设计值 / HAZID 校正

Run from repo root:
  python3 "docs/Design/Phase 3/D3.5-arch-hazid-backfill/evidence/extract_params.py"
"""
import csv
import re
import sys
from pathlib import Path

ARCH_DOC = Path(
    "docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md"
)
FCB_YAML = Path(
    "src/sim_workbench/fcb_simulator/config/fcb_dynamics.yaml"
)
OUTPUT = Path(
    "docs/Design/Phase 3/D3.5-arch-hazid-backfill/evidence/param_catalog_v1.csv"
)

FIELDNAMES = [
    "param_id", "source_doc", "section", "current_value", "unit",
    "iec61508_class", "aou_range", "hazid_priority", "manifest_sync",
]

params: dict[str, dict] = {}


def slugify(text: str) -> str:
    text = re.sub(r'[^\w\s\-]', '', text)
    text = re.sub(r'\s+', '_', text.strip())
    return text[:60]


def add_param(param_id: str, *, source_doc: str, section: str,
              current_value: str = "", unit: str = "",
              iec61508_class: str = "[manual review]",
              aou_range: str = "", hazid_priority: str = "[manual review]",
              manifest_sync: str = "N") -> None:
    if param_id in params:
        return
    params[param_id] = {
        "param_id": param_id,
        "source_doc": source_doc,
        "section": section,
        "current_value": current_value,
        "unit": unit,
        "iec61508_class": iec61508_class,
        "aou_range": aou_range,
        "hazid_priority": hazid_priority,
        "manifest_sync": manifest_sync,
    }


def parse_appendix_e(text: str, source_doc: str) -> None:
    in_e = False
    e_section = "E.?"
    header_passed = False

    for line in text.split('\n'):
        if '## 附录 E' in line:
            in_e = True
            continue
        if in_e and re.match(r'^## 附录 D', line):
            in_e = False
            continue
        if not in_e:
            continue

        sec_m = re.match(r'^###\s+(E\.\d+)\s+(.*)', line)
        if sec_m:
            e_section = sec_m.group(1)
            header_passed = False
            continue

        if re.match(r'^\|\s*参数\s*\|', line):
            header_passed = True
            continue
        if re.match(r'^\|[-|]+\|', line):
            continue
        if not header_passed:
            continue

        m_row = re.match(r'^\|([^|]+)\|([^|]+)\|([^|]+)\|([^|]+)\|([^|]+)\|', line)
        if not m_row:
            continue

        name = m_row.group(1).strip()
        sec_ref = m_row.group(2).strip()
        init_val = m_row.group(3).strip()
        priority = m_row.group(5).strip()

        if not name or name.startswith('---') or name in ('参数', '**参数**'):
            continue

        unit = ""
        for token, u in [(" kn", "kn"), ("nm", "nm"), ("°/s", "°/s"),
                         (" s）", "s"), (" s", "s"), (" m", "m"),
                         (" min", "min"), ("%", "%"), ("°", "°")]:
            if token in init_val:
                unit = u
                break

        add_param(
            f"APE_{e_section}_{slugify(name)}",
            source_doc=source_doc,
            section=f"AppE/{e_section}/{sec_ref}",
            current_value=init_val,
            unit=unit,
            hazid_priority=priority,
        )


def parse_yaml(text: str, source_doc: str) -> None:
    for line in text.split('\n'):
        if '[HAZID-UNVERIFIED' not in line:
            continue
        m = re.match(
            r'\s+(\w+):\s*([\d.eE+\-]+)\s*#\s*([^[]*)\[HAZID-UNVERIFIED', line
        )
        if not m:
            continue
        name, value, comment = m.group(1), m.group(2), m.group(3).strip()
        unit_m = re.match(r'^([a-zA-Z°/%]+),?\s', comment)
        unit = unit_m.group(1) if unit_m else ""
        add_param(
            f"YAML_{name}",
            source_doc=source_doc,
            section="fcb_dynamics",
            current_value=value,
            unit=unit,
        )


INLINE_MARKERS = ('[TBD-HAZID]', '[初始设计值]', '[HAZID 校正]', '[HAZID 校准]')


def parse_arch_body(text: str, source_doc: str) -> None:
    section = "unknown"
    section_re = re.compile(r'^#{1,3}\s+(.+)')

    for line in text.split('\n'):
        m_sec = section_re.match(line)
        if m_sec:
            heading = m_sec.group(1)
            sec_m = re.search(r'§[\d.]+', heading)
            section = sec_m.group(0) if sec_m else heading[:30]
            continue

        if not any(mk in line for mk in INLINE_MARKERS):
            continue

        if line.startswith('|'):
            cells = [c.strip() for c in line.split('|')[1:-1]]
            if len(cells) < 2:
                continue
            raw_name = re.sub(r'\[.*?\]|\*+', '', cells[0]).strip()
            if not raw_name or raw_name.startswith('---') or raw_name in ('参数', 'param'):
                continue
            value = re.sub(r'\[.*?\]', '', cells[1]).strip() if len(cells) > 1 else ""
            pid = f"ARCH_{section}_{slugify(raw_name)}"
            add_param(pid, source_doc=source_doc, section=section, current_value=value)
        else:
            m_eq = re.search(
                r'([\w_]+)\s*=\s*([\d.]+\s*(?:kn|nm|°|m|s|min|%)?)\s*\[', line
            )
            if m_eq:
                name, value = m_eq.group(1).strip(), m_eq.group(2).strip()
                pid = f"ARCH_{section}_{slugify(name)}"
                add_param(pid, source_doc=source_doc, section=section, current_value=value)


def main() -> None:
    if not ARCH_DOC.exists():
        sys.exit(f"ERROR: {ARCH_DOC} not found - run from repo root")
    if not FCB_YAML.exists():
        sys.exit(f"ERROR: {FCB_YAML} not found - run from repo root")

    arch_text = ARCH_DOC.read_text(encoding='utf-8')
    yaml_text = FCB_YAML.read_text(encoding='utf-8')

    parse_appendix_e(arch_text, str(ARCH_DOC))
    parse_yaml(yaml_text, str(FCB_YAML))
    parse_arch_body(arch_text, str(ARCH_DOC))

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUTPUT, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=FIELDNAMES)
        writer.writeheader()
        for row in sorted(params.values(), key=lambda r: r['param_id']):
            writer.writerow(row)

    print(f"Wrote {len(params)} params -> {OUTPUT}")
    print("Next: Task A-T2 - apply spec-known classifications to CSV")


if __name__ == '__main__':
    main()
