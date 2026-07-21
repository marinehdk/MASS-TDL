"""Tests for strict and legacy COLREGs trace-time selection."""

from __future__ import annotations

import math

import pytest

from tools.sil.trace_time import (
    ClockAlignment,
    ClockTransform,
    EventTime,
    EventTimeSelectionError,
    event_time_s,
    select_event_time,
)


def _alignment(
    *,
    generation: int = 17,
    anchors: tuple[tuple[float, float], ...] = ((0.0, 0.0), (10.0, 10.0)),
    uncertainty_s: float = 0.1,
    transforms: dict[str, ClockTransform] | None = None,
    source_priority: tuple[str, ...] = ("sim_t", "gnc_t", "source_stamp", "wall_t"),
) -> ClockAlignment:
    return ClockAlignment(
        lifecycle_run_generation=generation,
        anchors=anchors,
        transforms={
            "sim_t": ClockTransform(
                "sim_t", 1.0, 0.0, 0.0, 0, True, "simulation", generation
            ),
            "gnc_t": ClockTransform(
                "gnc_t", 1.0, 0.0, 0.1, 2, True, "gnc", generation
            ),
            "source_stamp": ClockTransform(
                "source_stamp", 1.0, 0.0, 0.1, 2, True, "ros", generation
            ),
            "wall_t": ClockTransform(
                "wall_t", 1.0, 0.0, 0.1, 2, True, "wall", generation
            ),
        }
        if transforms is None
        else transforms,
        uncertainty_s=uncertainty_s,
        source_priority=source_priority,
    )


def _row(source: str, value: float, *, generation: int = 17) -> dict:
    domains = {
        "sim_t": "simulation",
        "gnc_t": "gnc",
        "wall_t": "wall",
    }
    return {
        "record_id": f"{source}-row",
        source: value,
        "source_domain": domains[source],
        "run_generation": generation,
    }


def test_strict_api_exports_typed_results_and_error_details():
    alignment = _alignment()

    selected = select_event_time(_row("sim_t", 12.5), alignment)

    assert isinstance(selected, EventTime)
    assert selected == EventTime(12.5, 12.5, "sim_t", alignment.alignment_id, 0.1)

    with pytest.raises(EventTimeSelectionError) as caught:
        select_event_time({"record_id": "missing", "run_generation": 17}, alignment)
    assert caught.value.record_id == "missing"
    assert caught.value.attempted_source is None
    assert caught.value.alignment_id == alignment.alignment_id
    assert caught.value.reason == "clock_missing"


def test_strict_selector_rejects_missing_alignment_explicitly():
    with pytest.raises(EventTimeSelectionError) as caught:
        select_event_time(_row("sim_t", 1.0))

    assert caught.value.reason == "clock_alignment_missing"
    assert caught.value.alignment_id == "<missing>"


def test_alignment_id_is_deterministic_for_transform_mapping_order():
    alignment = _alignment()
    reversed_transforms = dict(reversed(tuple(alignment.transforms.items())))

    reordered = _alignment(transforms=reversed_transforms)

    assert reordered.alignment_id == alignment.alignment_id
    assert len(alignment.alignment_id) == 64


def test_zero_is_valid_when_explicitly_observed():
    selected = select_event_time(_row("sim_t", 0.0), _alignment())

    assert selected.canonical_s == 0.0
    assert selected.raw_s == 0.0


def test_sim_identity_requires_matching_generation_and_domain():
    alignment = _alignment()

    with pytest.raises(EventTimeSelectionError, match="run_generation_mismatch"):
        select_event_time(_row("sim_t", 1.0, generation=16), alignment)
    with pytest.raises(EventTimeSelectionError, match="source_domain_mismatch"):
        select_event_time(
            {
                "record_id": "wrong-domain",
                "sim_t": 1.0,
                "source_domain": "gnc",
                "run_generation": 17,
            },
            alignment,
        )


def test_sim_clock_rejects_non_identity_transform():
    alignment = _alignment(
        transforms={
            "sim_t": ClockTransform(
                "sim_t", 0.5, 1.0, 0.0, 0, True, "simulation", 17
            )
        },
        source_priority=("sim_t",),
    )

    with pytest.raises(EventTimeSelectionError, match="transform_invalid"):
        select_event_time(_row("sim_t", 1.0), alignment)


def test_sim_identity_rejects_declared_anchor_count_without_coherent_fit():
    alignment = _alignment(
        anchors=((0.0, 0.0), (10.0, 11.0)),
        transforms={
            "sim_t": ClockTransform(
                "sim_t", 1.0, 0.0, 0.0, 99, True, "simulation", 17
            )
        },
        source_priority=("sim_t",),
    )

    with pytest.raises(EventTimeSelectionError, match="transform_invalid"):
        select_event_time(_row("sim_t", 1.0), alignment)


def test_sim_identity_accepts_declared_anchors_only_when_fit_is_coherent():
    alignment = _alignment(
        anchors=((0.0, 0.0), (10.0, 10.0)),
        transforms={
            "sim_t": ClockTransform(
                "sim_t", 1.0, 0.0, 0.0, 2, True, "simulation", 17
            )
        },
        source_priority=("sim_t",),
    )

    assert select_event_time(_row("sim_t", 1.0), alignment).canonical_s == 1.0


@pytest.mark.parametrize("anchor_count", [-1, True, 0.0, "0"])
def test_sim_clock_requires_nonnegative_strict_integer_anchor_count(anchor_count):
    alignment = _alignment(
        transforms={
            "sim_t": ClockTransform(
                "sim_t", 1.0, 0.0, 0.0, anchor_count, True, "simulation", 17
            )
        },
        source_priority=("sim_t",),
    )

    with pytest.raises(EventTimeSelectionError, match="transform_invalid"):
        select_event_time(_row("sim_t", 1.0), alignment)


@pytest.mark.parametrize(
    ("transform", "anchors", "uncertainty_s", "reason"),
    [
        (ClockTransform("gnc_t", 1.0, 0.0, 0.1, 1, True, "gnc", 17), ((0.0, 0.0),), 0.1, "transform_invalid"),
        (ClockTransform("gnc_t", math.nan, 0.0, 0.1, 2, True, "gnc", 17), ((0.0, 0.0), (1.0, 1.0)), 0.1, "transform_invalid"),
        (ClockTransform("gnc_t", 1.0, 0.0, 0.500001, 2, True, "gnc", 17), ((0.0, 0.0), (1.0, 1.0)), 0.1, "transform_invalid"),
        (ClockTransform("gnc_t", 1.0, 0.0, 0.1, 2, False, "gnc", 17), ((0.0, 0.0), (1.0, 1.0)), 0.1, "transform_invalid"),
        (ClockTransform("gnc_t", 1.0, 0.0, 0.1, 2, True, "gnc", 17), ((0.0, 0.0), (1.0, 1.0)), 1.000001, "uncertainty_invalid"),
    ],
)
def test_external_clocks_fail_closed_on_invalid_alignment(
    transform: ClockTransform,
    anchors: tuple[tuple[float, float], ...],
    uncertainty_s: float,
    reason: str,
):
    alignment = _alignment(
        anchors=anchors,
        transforms={"gnc_t": transform},
        uncertainty_s=uncertainty_s,
        source_priority=("gnc_t",),
    )

    with pytest.raises(EventTimeSelectionError, match=reason):
        select_event_time(_row("gnc_t", 1.0), alignment)


def test_external_clock_accepts_residual_and_uncertainty_boundaries():
    alignment = _alignment(
        anchors=((0.0, -1.0), (10.0, 19.0)),
        uncertainty_s=1.0,
        transforms={
            "gnc_t": ClockTransform(
                "gnc_t", 2.0, -1.0, 0.5, 2, True, "gnc", 17
            )
        },
        source_priority=("gnc_t",),
    )

    selected = select_event_time(_row("gnc_t", 3.0), alignment)

    assert selected.canonical_s == 5.0
    assert selected.uncertainty_s == 1.0


@pytest.mark.parametrize("scale", [0.0, -1.0])
def test_external_clock_requires_positive_scale(scale: float):
    alignment = _alignment(
        anchors=((0.0, 0.0), (10.0, 10.0)),
        transforms={
            "gnc_t": ClockTransform(
                "gnc_t", scale, 0.0, 0.1, 2, True, "gnc", 17
            )
        },
        source_priority=("gnc_t",),
    )

    with pytest.raises(EventTimeSelectionError, match="transform_invalid"):
        select_event_time(_row("gnc_t", 1.0), alignment)


@pytest.mark.parametrize(
    "transform",
    [
        ClockTransform("gnc_t", 2.0, 0.0, 0.1, 2, True, "gnc", 17),
        ClockTransform("gnc_t", 1.0, 0.4, 0.1, 2, True, "gnc", 17),
        ClockTransform("gnc_t", 1.0, 0.0, 0.1, 99, True, "gnc", 17),
    ],
)
def test_external_transform_must_fit_every_declared_anchor(transform: ClockTransform):
    alignment = _alignment(
        anchors=((0.0, 0.0), (10.0, 10.0)),
        transforms={"gnc_t": transform},
        source_priority=("gnc_t",),
    )

    with pytest.raises(EventTimeSelectionError, match="transform_invalid"):
        select_event_time(_row("gnc_t", 1.0), alignment)


@pytest.mark.parametrize(
    "alignment",
    [
        ClockAlignment(
            lifecycle_run_generation=17,
            anchors=((0.0, 0.0), (1.0, 1.0)),
            transforms={
                "sim_t": ClockTransform(
                    "sim_t", 1.0, 0.0, 0.0, math.nan, True, "simulation", 17
                )
            },
            uncertainty_s=0.1,
            source_priority=("sim_t",),
        ),
        ClockAlignment(
            lifecycle_run_generation=17,
            anchors=((0.0, 0.0), (1.0, 1.0)),
            transforms={"sim_t": object()},
            uncertainty_s=0.1,
            source_priority=(math.nan,),
        ),
    ],
)
def test_malformed_alignment_fields_raise_typed_selection_error(alignment):
    with pytest.raises(EventTimeSelectionError):
        select_event_time(_row("sim_t", 1.0), alignment)


@pytest.mark.parametrize("stamp_key", ["source_stamp", "stamp"])
def test_source_stamp_parsing_preserves_raw_value(stamp_key: str):
    row = {
        "record_id": stamp_key,
        stamp_key: {"sec": 7, "nanosec": 500_000_000},
        "source_domain": "ros",
        "run_generation": 17,
    }

    selected = select_event_time(row, _alignment())

    assert selected.source == "source_stamp"
    assert selected.raw_s == 7.5
    assert selected.canonical_s == 7.5


@pytest.mark.parametrize(
    ("row", "reason"),
    [
        ({"record_id": "missing", "run_generation": 17}, "clock_missing"),
        ({"record_id": "nan", "sim_t": math.nan, "source_domain": "simulation", "run_generation": 17}, "invalid_declared_clock"),
        ({"record_id": "inf", "sim_t": math.inf, "source_domain": "simulation", "run_generation": 17}, "invalid_declared_clock"),
        ({"record_id": "bad-stamp", "source_stamp": {"sec": "bad", "nanosec": 0}, "source_domain": "ros", "run_generation": 17}, "invalid_declared_clock"),
        ({"record_id": "bad-nanos", "source_stamp": {"sec": 1, "nanosec": 1_000_000_000}, "source_domain": "ros", "run_generation": 17}, "invalid_declared_clock"),
        ({"record_id": "generation", "gnc_t": 1.0, "source_domain": "gnc", "run_generation": 18}, "run_generation_mismatch"),
        ({"record_id": "domain", "gnc_t": 1.0, "source_domain": "wall", "run_generation": 17}, "source_domain_mismatch"),
    ],
)
def test_missing_malformed_and_mismatched_time_fail_explicitly(row: dict, reason: str):
    with pytest.raises(EventTimeSelectionError, match=reason) as caught:
        select_event_time(row, _alignment())
    assert row["record_id"] in str(caught.value)


def test_invalid_higher_priority_declared_clock_does_not_fall_through():
    row = {
        "record_id": "priority",
        "sim_t": math.nan,
        "gnc_t": 42.25,
        "source_domain": "simulation",
        "run_generation": 17,
    }

    with pytest.raises(EventTimeSelectionError, match="attempted_source=sim_t"):
        select_event_time(row, _alignment())


def test_unregistered_declared_clock_fails_explicitly():
    alignment = _alignment(transforms={}, source_priority=("gnc_t",))

    with pytest.raises(EventTimeSelectionError, match="transform_unregistered"):
        select_event_time(_row("gnc_t", 1.0), alignment)


def test_present_clock_missing_from_declared_priority_fails_explicitly():
    alignment = _alignment(source_priority=("gnc_t",))

    with pytest.raises(EventTimeSelectionError, match="source_priority_undeclared"):
        select_event_time(_row("sim_t", 1.0), alignment)


def test_alignment_sequence_rejects_canonical_backjump():
    alignment = _alignment()

    with pytest.raises(EventTimeSelectionError, match="canonical_time_backjump"):
        alignment.select_sequence([_row("sim_t", 2.0), _row("sim_t", 1.0)])


def test_new_active_generation_uses_a_new_sequence_and_alignment():
    first = _alignment(generation=17)
    reset = _alignment(generation=18)
    first_rows = [_row("sim_t", 9.0, generation=17)]
    reset_rows = [
        {
            **_row("sim_t", 0.0, generation=18),
            "lifecycle_state": "ACTIVE",
        }
    ]

    assert first.select_sequence(first_rows)[0].canonical_s == 9.0
    assert reset.select_sequence(reset_rows)[0].canonical_s == 0.0


def test_legacy_event_time_api_remains_available_for_non_g1_callers():
    assert event_time_s({}) == 0.0
    assert event_time_s({"sim_t": 0.0, "gnc_t": 42.25}) == 42.25
