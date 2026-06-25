"""Tests for the ROS2 interface contract checker."""
import textwrap
from pathlib import Path

import pytest

from tools.sil.check_ros2_interface_contract import (
    TopicContract,
    SourceFinding,
    check_source_against_contract,
)


@pytest.fixture
def sample_contract_yaml(tmp_path):
    yaml_text = textwrap.dedent("""\
        topic_contract_version: 1
        legacy_allowed_until: "2026-07-15"
        topics:
          - { name: /l3/m5/avoidance_plan, type: l3_msgs/msg/AvoidancePlan, owner: M5, qos: state_stream }
          - { name: /l3/m5/reactive_override_cmd, type: l3_msgs/msg/ReactiveOverrideCmd, owner: M5, qos: short_command }
        legacy_topics:
          - { old: /m5/avoidance_plan, new: /l3/m5/avoidance_plan }
        """)
    p = tmp_path / "contract.yaml"
    p.write_text(yaml_text)
    return p


def test_load_contract(sample_contract_yaml):
    c = TopicContract.load(sample_contract_yaml)
    assert "/l3/m5/avoidance_plan" in c.topics
    assert c.topics["/l3/m5/avoidance_plan"].type == "l3_msgs/msg/AvoidancePlan"
    assert "/m5/avoidance_plan" in c.legacy_old_names


def test_finding_canonical_ok(sample_contract_yaml):
    c = TopicContract.load(sample_contract_yaml)
    findings = [
        SourceFinding(path="x.cpp", line=10, kind="publisher", topic="/l3/m5/avoidance_plan", ros_type="l3_msgs/msg/AvoidancePlan"),
    ]
    violations = check_source_against_contract(c, findings)
    assert violations == []


def test_finding_unregistered_topic_violation(sample_contract_yaml):
    c = TopicContract.load(sample_contract_yaml)
    findings = [
        SourceFinding(path="x.cpp", line=10, kind="publisher", topic="/l3/m9/unknown", ros_type="l3_msgs/msg/Foo"),
    ]
    violations = check_source_against_contract(c, findings)
    assert len(violations) == 1
    assert "unregistered" in violations[0]


def test_finding_type_mismatch_violation(sample_contract_yaml):
    c = TopicContract.load(sample_contract_yaml)
    findings = [
        SourceFinding(path="x.cpp", line=10, kind="publisher", topic="/l3/m5/avoidance_plan", ros_type="l3_msgs/msg/WrongType"),
    ]
    violations = check_source_against_contract(c, findings)
    assert len(violations) == 1
    assert "type" in violations[0].lower()


def test_finding_legacy_topic_outside_whitelist_violation(sample_contract_yaml):
    c = TopicContract.load(sample_contract_yaml)
    # /m2/foo is neither canonical nor in legacy_topics whitelist
    findings = [
        SourceFinding(path="x.cpp", line=10, kind="publisher", topic="/m2/foo", ros_type="l3_msgs/msg/Foo"),
    ]
    violations = check_source_against_contract(c, findings)
    assert len(violations) == 1
    assert "unregistered" in violations[0]
