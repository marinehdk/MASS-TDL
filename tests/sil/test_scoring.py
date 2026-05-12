"""Tests for the ScoringNode — 6-dim Hagen 2022 scoring."""

from scoring.node import ScoringNode


def test_scoring_produces_all_dims() -> None:
    node = ScoringNode()
    row = node.score(0.0)
    for d in ScoringNode.DIMS:
        assert d in row, f"missing dimension {d}"
    assert "total" in row


def test_weights_sum_to_one() -> None:
    node = ScoringNode()
    total = sum(node._weights.values())
    assert abs(total - 1.0) < 0.01, f"weights sum to {total}"


def test_perfect_score() -> None:
    node = ScoringNode()
    row = node.score(0.0, cpa=1.5, rule_broken=False, delay_s=0, path_deviation=0)
    assert row["total"] > 0.9, f"perfect score too low: {row['total']}"


def test_rule_broken_zeros_compliance() -> None:
    node = ScoringNode()
    row = node.score(0.0, rule_broken=True)
    assert row["rule_compliance"] == 0.0


def test_final_verdict_empty() -> None:
    node = ScoringNode()
    passed, avg = node.get_final_verdict()
    assert passed is False
    assert avg == 0.0


def test_final_verdict_passes() -> None:
    node = ScoringNode()
    for i in range(10):
        node.score(float(i), cpa=1.5, rule_broken=False)
    passed, avg = node.get_final_verdict(threshold=0.7)
    assert passed is True, f"avg={avg} should pass"
