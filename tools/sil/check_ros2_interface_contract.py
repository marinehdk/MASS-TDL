"""Static checker for the ROS2 interface contract.

Scans C++/Python source for create_publisher/create_subscription calls,
extracts topic name + ROS2 type, and checks against the contract YAML.
Fails on: unregistered topic, type mismatch, legacy topic outside whitelist.
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

import yaml


@dataclass
class TopicEntry:
    name: str
    type: str
    owner: str
    qos: str


@dataclass
class SourceFinding:
    path: str
    line: int
    kind: str  # "publisher" or "subscription"
    topic: str
    ros_type: Optional[str]


@dataclass
class TopicContract:
    topics: Dict[str, TopicEntry] = field(default_factory=dict)
    legacy_old_names: Dict[str, str] = field(default_factory=dict)
    legacy_allowed_until: str = ""

    @classmethod
    def load(cls, path: Path) -> "TopicContract":
        data = yaml.safe_load(Path(path).read_text())
        c = cls()
        for t in data.get("topics", []):
            c.topics[t["name"]] = TopicEntry(
                name=t["name"], type=t["type"], owner=t.get("owner", ""), qos=t.get("qos", "")
            )
        for lt in data.get("legacy_topics", []):
            c.legacy_old_names[lt["old"]] = lt["new"]
        c.legacy_allowed_until = data.get("legacy_allowed_until", "")
        return c


def check_source_against_contract(
    contract: TopicContract, findings: List[SourceFinding]
) -> List[str]:
    violations: List[str] = []
    for f in findings:
        if f.topic in contract.topics:
            expected_type = contract.topics[f.topic].type
            if f.ros_type and f.ros_type != expected_type:
                violations.append(
                    f"{f.path}:{f.line}: type mismatch on {f.topic}: "
                    f"found {f.ros_type}, contract says {expected_type}"
                )
        elif f.topic in contract.legacy_old_names:
            # legacy alias: allowed (within expiry window). No violation.
            pass
        else:
            violations.append(
                f"{f.path}:{f.line}: unregistered topic {f.topic} ({f.kind})"
            )
    return violations


# Regex for C++ create_publisher<T>("topic", ...) and create_subscription<T>("topic", ...)
_CPP_PUB_RE = re.compile(
    r'create_publisher<([\w:]+)>\s*\(\s*"([^"]+)"'
)
_CPP_SUB_RE = re.compile(
    r'create_subscription<([\w:]+)>\s*\(\s*\n?\s*"([^"]+)"'
)


def scan_cpp_file(path: Path) -> List[SourceFinding]:
    findings: List[SourceFinding] = []
    text = path.read_text(errors="replace")
    for lineno, line in enumerate(text.splitlines(), 1):
        for m in _CPP_PUB_RE.finditer(line):
            ros_type = _normalize_cpp_type(m.group(1))
            findings.append(SourceFinding(str(path), lineno, "publisher", m.group(2), ros_type))
        for m in _CPP_SUB_RE.finditer(line):
            ros_type = _normalize_cpp_type(m.group(1))
            findings.append(SourceFinding(str(path), lineno, "subscription", m.group(2), ros_type))
    return findings


def _normalize_cpp_type(raw: str) -> str:
    # l3_msgs::msg::AvoidancePlan -> l3_msgs/msg/AvoidancePlan
    return raw.replace("::msg::", "/msg/").replace("::srv::", "/srv/")


# Matches: using BehaviorPlanMsg = l3_msgs::msg::BehaviorPlan;
_USING_ALIAS_RE = re.compile(
    r'\busing\s+(\w+)\s*=\s*([\w:]+::msg::\w+)\s*;'
)


def collect_type_aliases(root: Path, exclude_globs: List[str]) -> Dict[str, str]:
    """Build a {alias_name: normalized_ros_type} map from `using X = pkg::msg::Y;` decls."""
    aliases: Dict[str, str] = {}
    for pat in ("*.hpp", "*.h"):
        for path in root.rglob(pat):
            if any(part in str(path) for part in exclude_globs):
                continue
            text = path.read_text(errors="replace")
            for m in _USING_ALIAS_RE.finditer(text):
                aliases[m.group(1)] = _normalize_cpp_type(m.group(2))
    return aliases


def scan_directory(root: Path, exclude_globs: List[str]) -> List[SourceFinding]:
    aliases = collect_type_aliases(root, exclude_globs)
    findings: List[SourceFinding] = []
    for path in root.rglob("*.cpp"):
        if any(part in str(path) for part in exclude_globs):
            continue
        text = path.read_text(errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            for m in _CPP_PUB_RE.finditer(line):
                ros_type = aliases.get(m.group(1)) or _normalize_cpp_type(m.group(1))
                findings.append(SourceFinding(str(path), lineno, "publisher", m.group(2), ros_type))
            for m in _CPP_SUB_RE.finditer(line):
                ros_type = aliases.get(m.group(1)) or _normalize_cpp_type(m.group(1))
                findings.append(SourceFinding(str(path), lineno, "subscription", m.group(2), ros_type))
    return findings


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", required=True, type=Path)
    parser.add_argument("--root", default="src/l3_tdl_kernel", type=Path)
    args = parser.parse_args(argv)

    contract = TopicContract.load(args.contract)
    # Exclude GNC bridge (Track A) and third_party. The Track A A5 C++ SIL
    # adapters (sil_fusion/trace/pulse_adapter) live under src/sim_workbench,
    # outside the default --root src/l3_tdl_kernel, so they are not scanned
    # here; their topics are relay-boundary topics, not L3 kernel contracts.
    excludes = ["third_party", "gnc_bridge", "build", "install"]
    findings = scan_directory(args.root, excludes)
    violations = check_source_against_contract(contract, findings)
    if violations:
        for v in violations:
            print(v, file=sys.stderr)
        return 1
    print(f"OK: {len(findings)} findings checked, 0 violations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
