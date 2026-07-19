"""Canonical event-time selection for COLREGs evidence.

Strict G1 callers use :func:`select_event_time` with a declared
``ClockAlignment``. ``event_time_s`` remains only for legacy, non-G1 trace
readers whose input contract predates lifecycle clock alignment.
"""

from __future__ import annotations

import json
import math
from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from hashlib import sha256
from numbers import Real
from typing import Any


MAX_TRANSFORM_RESIDUAL_S = 0.5
MAX_EVENT_UNCERTAINTY_S = 1.0
_KNOWN_CLOCK_SOURCES = (
    "recorder_t", "sim_t", "gnc_t", "source_stamp", "source_t", "wall_t"
)


def _finite(value: Any) -> float | None:
    """Legacy numeric coercion used only by :func:`event_time_s`."""
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def _strict_finite(value: Any) -> float | None:
    if isinstance(value, bool) or not isinstance(value, Real):
        return None
    number = float(value)
    return number if math.isfinite(number) else None


def _strict_int(value: Any) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int):
        return None
    return value


def _canonical_number(value: Any) -> float | str:
    number = _strict_finite(value)
    return number if number is not None else _invalid_token(value)


def _invalid_token(value: Any) -> str:
    try:
        rendered = repr(value)
    except Exception:
        rendered = f"<{type(value).__name__}>"
    return f"<invalid:{rendered}>"


def _canonical_int(value: Any) -> int | str:
    number = _strict_int(value)
    return number if number is not None else _invalid_token(value)


def _canonical_text(value: Any) -> str:
    return value if isinstance(value, str) else _invalid_token(value)


def _canonical_bool(value: Any) -> bool | str:
    return value if isinstance(value, bool) else _invalid_token(value)


@dataclass(frozen=True)
class ClockTransform:
    source: str
    scale: float
    offset_s: float
    residual_s: float
    anchor_count: int
    valid: bool
    source_domain: str
    run_generation: int


@dataclass(frozen=True)
class EventTime:
    canonical_s: float
    raw_s: float
    source: str
    alignment_id: str
    uncertainty_s: float

    def as_dict(self) -> dict[str, float | str]:
        return {
            "canonical_s": self.canonical_s,
            "raw_s": self.raw_s,
            "source": self.source,
            "alignment_id": self.alignment_id,
            "uncertainty_s": self.uncertainty_s,
        }


class EventTimeSelectionError(ValueError):
    """A declared clock cannot yield a valid canonical event time."""

    def __init__(
        self,
        record_id: str,
        attempted_source: str | None,
        alignment_id: str,
        uncertainty_s: float,
        reason: str,
    ) -> None:
        self.record_id = record_id
        self.attempted_source = attempted_source
        self.alignment_id = alignment_id
        self.uncertainty_s = uncertainty_s
        self.reason = reason
        super().__init__(
            f"record_id={record_id}; attempted_source={attempted_source}; "
            f"alignment_id={alignment_id}; uncertainty_s={uncertainty_s}; "
            f"reason={reason}"
        )


def _error_uncertainty(alignment: ClockAlignment) -> float:
    uncertainty_s = _strict_finite(alignment.uncertainty_s)
    return (
        uncertainty_s
        if uncertainty_s is not None
        else MAX_EVENT_UNCERTAINTY_S + 1.0
    )


@dataclass(frozen=True)
class ClockAlignment:
    lifecycle_run_generation: int
    anchors: tuple[tuple[float, float], ...]
    transforms: Mapping[str, ClockTransform]
    uncertainty_s: float
    source_priority: tuple[str, ...]

    @property
    def alignment_id(self) -> str:
        anchors: list[Any] = []
        if isinstance(self.anchors, (tuple, list)):
            for pair in self.anchors:
                if isinstance(pair, (tuple, list)) and len(pair) == 2:
                    anchors.append(
                        [_canonical_number(pair[0]), _canonical_number(pair[1])]
                    )
                else:
                    anchors.append(_invalid_token(pair))
        else:
            anchors.append(_invalid_token(self.anchors))

        transform_entries: list[tuple[str, Any]] = []
        if isinstance(self.transforms, Mapping):
            for source, transform in self.transforms.items():
                canonical_source = _canonical_text(source)
                if not isinstance(transform, ClockTransform):
                    payload: Any = _invalid_token(transform)
                else:
                    payload = {
                        "anchor_count": _canonical_int(transform.anchor_count),
                        "offset_s": _canonical_number(transform.offset_s),
                        "residual_s": _canonical_number(transform.residual_s),
                        "run_generation": _canonical_int(transform.run_generation),
                        "scale": _canonical_number(transform.scale),
                        "source": _canonical_text(transform.source),
                        "source_domain": _canonical_text(transform.source_domain),
                        "valid": _canonical_bool(transform.valid),
                    }
                transform_entries.append((canonical_source, payload))
        else:
            transform_entries.append(("<registry>", _invalid_token(self.transforms)))
        transform_entries.sort(key=lambda item: item[0])
        transforms = {source: transform for source, transform in transform_entries}

        if isinstance(self.source_priority, (tuple, list)):
            source_priority = [
                _canonical_text(source) for source in self.source_priority
            ]
        else:
            source_priority = [_invalid_token(self.source_priority)]
        payload = {
            "anchors": anchors,
            "lifecycle_run_generation": _canonical_int(
                self.lifecycle_run_generation
            ),
            "source_priority": source_priority,
            "transforms": transforms,
        }
        canonical_json = json.dumps(
            payload,
            ensure_ascii=True,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        return sha256(canonical_json.encode("utf-8")).hexdigest()

    def select_sequence(
        self,
        rows: Iterable[Mapping[str, Any]],
    ) -> tuple[EventTime, ...]:
        """Select one monotonic ACTIVE-generation sequence.

        A lifecycle ACTIVE reset must use a new alignment for its new
        generation. Backjumps inside one alignment are evidence failures.
        """
        selected: list[EventTime] = []
        previous: EventTime | None = None
        for row in rows:
            current = select_event_time(row, self)
            if previous is not None and current.canonical_s < previous.canonical_s:
                raise EventTimeSelectionError(
                    str(row.get("record_id", "<unknown>")),
                    current.source,
                    self.alignment_id,
                    _error_uncertainty(self),
                    "canonical_time_backjump",
                )
            selected.append(current)
            previous = current
        return tuple(selected)


def _error(
    row: Mapping[str, Any],
    alignment: ClockAlignment,
    attempted_source: str | None,
    reason: str,
) -> EventTimeSelectionError:
    return EventTimeSelectionError(
        str(row.get("record_id", "<unknown>")),
        attempted_source,
        alignment.alignment_id,
        _error_uncertainty(alignment),
        reason,
    )


def _stamp_value(row: Mapping[str, Any]) -> tuple[bool, float | None]:
    if "source_stamp" in row:
        stamp = row["source_stamp"]
    elif "stamp" in row:
        stamp = row["stamp"]
    else:
        return False, None
    if not isinstance(stamp, Mapping):
        return True, None
    sec = _strict_int(stamp.get("sec", stamp.get("seconds")))
    nanosec = _strict_int(stamp.get("nanosec", stamp.get("nanos")))
    if sec is None or nanosec is None or not 0 <= nanosec < 1_000_000_000:
        return True, None
    return True, sec + nanosec * 1.0e-9


def _declared_clock_value(
    row: Mapping[str, Any],
    source: str,
) -> tuple[bool, float | None]:
    if source == "source_stamp":
        return _stamp_value(row)
    if source not in row:
        return False, None
    return True, _strict_finite(row[source])


def _present_known_sources(row: Mapping[str, Any]) -> set[str]:
    present = {source for source in _KNOWN_CLOCK_SOURCES if source in row}
    if "stamp" in row:
        present.add("source_stamp")
    return present


def _anchors_are_valid(anchors: tuple[tuple[float, float], ...]) -> bool:
    return isinstance(anchors, (tuple, list)) and len(anchors) >= 2 and all(
        isinstance(pair, (tuple, list))
        and len(pair) == 2
        and _strict_finite(pair[0]) is not None
        and _strict_finite(pair[1]) is not None
        for pair in anchors
    )


def _transform_is_valid(
    source: str,
    transform: ClockTransform,
    alignment: ClockAlignment,
) -> bool:
    scale = _strict_finite(transform.scale)
    offset_s = _strict_finite(transform.offset_s)
    residual_s = _strict_finite(transform.residual_s)
    anchor_count = _strict_int(transform.anchor_count)
    common_valid = (
        transform.source == source
        and isinstance(transform.source_domain, str)
        and bool(transform.source_domain)
        and transform.valid is True
        and scale is not None
        and scale > 0.0
        and offset_s is not None
        and residual_s is not None
        and 0.0 <= residual_s <= MAX_TRANSFORM_RESIDUAL_S
        and anchor_count is not None
        and anchor_count >= 0
        and _strict_int(transform.run_generation) == alignment.lifecycle_run_generation
    )
    if not common_valid:
        return False
    if source == "sim_t":
        if scale != 1.0 or offset_s != 0.0:
            return False
        if anchor_count == 0:
            return True
        if (
            not _anchors_are_valid(alignment.anchors)
            or anchor_count != len(alignment.anchors)
        ):
            return False
        actual_residual_s = max(
            abs(float(raw_s) - float(canonical_s))
            for raw_s, canonical_s in alignment.anchors
        )
        return actual_residual_s <= residual_s + 1.0e-12
    if (
        anchor_count is None
        or anchor_count < 2
        or not _anchors_are_valid(alignment.anchors)
        or anchor_count != len(alignment.anchors)
    ):
        return False
    actual_residual_s = max(
        abs(scale * float(raw_s) + offset_s - float(canonical_s))
        for raw_s, canonical_s in alignment.anchors
    )
    return actual_residual_s <= residual_s + 1.0e-12


def select_event_time(
    row: Mapping[str, Any],
    alignment: ClockAlignment | None = None,
) -> EventTime:
    """Select the highest-priority present clock and align it fail closed."""
    if not isinstance(alignment, ClockAlignment):
        raise EventTimeSelectionError(
            str(row.get("record_id", "<unknown>")),
            None,
            "<missing>",
            MAX_EVENT_UNCERTAINTY_S + 1.0,
            "clock_alignment_missing",
        )
    priority = alignment.source_priority
    if (
        not isinstance(priority, tuple)
        or not priority
        or any(not isinstance(source, str) or not source for source in priority)
        or len(set(priority)) != len(priority)
    ):
        raise _error(row, alignment, None, "source_priority_invalid")

    alignment_generation = _strict_int(alignment.lifecycle_run_generation)
    if alignment_generation is None:
        raise _error(row, alignment, None, "run_generation_mismatch")

    uncertainty_s = _strict_finite(alignment.uncertainty_s)
    if uncertainty_s is None or not 0.0 <= uncertainty_s <= MAX_EVENT_UNCERTAINTY_S:
        raise _error(row, alignment, None, "uncertainty_invalid")

    undeclared = _present_known_sources(row) - set(priority)
    if undeclared:
        attempted = next(
            source for source in _KNOWN_CLOCK_SOURCES if source in undeclared
        )
        raise _error(row, alignment, attempted, "source_priority_undeclared")

    for source in priority:
        present, raw_s = _declared_clock_value(row, source)
        if not present:
            continue
        if raw_s is None:
            raise _error(row, alignment, source, "invalid_declared_clock")

        transform = (
            alignment.transforms.get(source)
            if isinstance(alignment.transforms, Mapping)
            else None
        )
        if not isinstance(transform, ClockTransform):
            raise _error(row, alignment, source, "transform_unregistered")

        row_generation = _strict_int(row.get("run_generation"))
        if (
            row_generation != alignment_generation
            or transform.run_generation != alignment_generation
        ):
            raise _error(row, alignment, source, "run_generation_mismatch")

        source_domain = row.get("source_domain", row.get("clock_domain"))
        if (
            "source_domain" in row
            and "clock_domain" in row
            and row["source_domain"] != row["clock_domain"]
        ):
            raise _error(row, alignment, source, "source_domain_mismatch")
        if (
            not isinstance(source_domain, str)
            or not source_domain
            or source_domain != transform.source_domain
        ):
            raise _error(row, alignment, source, "source_domain_mismatch")

        if not _transform_is_valid(source, transform, alignment):
            raise _error(row, alignment, source, "transform_invalid")

        canonical_s = transform.scale * raw_s + transform.offset_s
        if not math.isfinite(canonical_s):
            raise _error(row, alignment, source, "canonical_time_invalid")
        return EventTime(
            canonical_s=canonical_s,
            raw_s=raw_s,
            source=source,
            alignment_id=alignment.alignment_id,
            uncertainty_s=uncertainty_s,
        )

    raise _error(row, alignment, None, "clock_missing")


def event_time_s(row: Mapping[str, Any]) -> float:
    """Legacy best-effort selector retained for non-G1 callers.

    This API may return an invented zero and must never be used by strict G1
    qualification paths. New evidence code must call :func:`select_event_time`.
    """
    zero_candidate: float | None = None
    for key in ("sim_t", "gnc_t", "source_t", "wall_t"):
        value = _finite(row.get(key))
        if value is None:
            continue
        if value > 0.0:
            return value
        zero_candidate = value
    stamp = row.get("stamp")
    if isinstance(stamp, Mapping):
        sec = _finite(stamp.get("sec"))
        nanosec = _finite(stamp.get("nanosec"))
        if sec is not None and nanosec is not None:
            return sec + nanosec * 1.0e-9
    return 0.0 if zero_candidate is not None else 0.0
