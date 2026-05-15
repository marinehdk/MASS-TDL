"""Tests for ArrowWriter — side-channel Arrow IPC file writer.

Verifies:
  - Single-row write and read-back via polars
  - Multi-row batch write
  - Batch flush threshold behavior
"""

import tempfile
from pathlib import Path

import pytest

from scoring.arrow_writer import ArrowWriter
from scoring.hagen_scorer import ScoringRow


class TestArrowWriter:
    """ArrowWriter TDD test suite — test first, fail, then implement."""

    def test_write_and_read_single_row(self) -> None:
        """Write one ScoringRow, close, then read back with polars."""
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "scoring.arrow"
            with ArrowWriter(str(path)) as writer:
                row = ScoringRow(
                    stamp=100.0,
                    safety=0.92,
                    rule_compliance=1.0,
                    delay_penalty=0.0,
                    action_magnitude_penalty=0.0,
                    phase_score=1.0,
                    plausibility=0.95,
                    total=0.91,
                    cpa_nm=0.42,
                    cpa_target_nm=0.27,
                )
                writer.append(row)

            assert path.exists()
            assert path.stat().st_size > 0

            import polars as pl

            df = pl.read_ipc(str(path))
            assert len(df) == 1
            assert abs(df["total"][0] - 0.91) < 0.001

    def test_write_multiple_rows(self) -> None:
        """Write 100 rows, close, verify stamp ordering preserved."""
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "scoring.arrow"
            with ArrowWriter(str(path)) as writer:
                for i in range(100):
                    row = ScoringRow(
                        stamp=float(i),
                        safety=0.9,
                        rule_compliance=1.0,
                        delay_penalty=0.0,
                        action_magnitude_penalty=0.0,
                        phase_score=1.0,
                        plausibility=1.0,
                        total=0.95,
                        cpa_nm=0.5,
                        cpa_target_nm=0.27,
                    )
                    writer.append(row)

            import polars as pl

            df = pl.read_ipc(str(path))
            assert len(df) == 100
            assert df["stamp"].to_list() == list(range(100))

    def test_buffer_flushes_at_threshold(self) -> None:
        """Buffer auto-flushes when batch_size is reached."""
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "scoring.arrow"
            with ArrowWriter(str(path), batch_size=10) as writer:
                row = ScoringRow(
                    stamp=0.0,
                    safety=1.0,
                    rule_compliance=1.0,
                    delay_penalty=0.0,
                    action_magnitude_penalty=0.0,
                    phase_score=1.0,
                    plausibility=1.0,
                    total=1.0,
                    cpa_nm=1.0,
                    cpa_target_nm=0.27,
                )
                # Append exactly batch_size rows — should trigger auto-flush
                for _ in range(10):
                    writer.append(row)

            assert path.exists()
            assert path.stat().st_size > 0

            import polars as pl

            df = pl.read_ipc(str(path))
            assert len(df) == 10
