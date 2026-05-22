"""Tests for mcap_to_arrow — pytest tools/vv/test_mcap_to_arrow.py"""
import pytest
from pathlib import Path


def test_empty_mcap_produces_arrow(tmp_path):
    """If MCAP doesn't exist, converter writes empty Arrow file (not crash)."""
    from tools.vv.mcap_to_arrow import convert_mcap_to_arrow
    out = tmp_path / "replay.arrow"
    convert_mcap_to_arrow(str(tmp_path / "nonexistent.mcap"), str(out),
                          topics=["/sil/own_ship_state"])
    assert out.exists()
    import pyarrow.ipc as pa_ipc
    reader = pa_ipc.open_file(str(out))
    assert reader.schema is not None


def test_schema_columns(tmp_path):
    from tools.vv.mcap_to_arrow import convert_mcap_to_arrow
    out = tmp_path / "replay.arrow"
    convert_mcap_to_arrow(str(tmp_path / "nonexistent.mcap"), str(out))
    import pyarrow.ipc as pa_ipc
    reader = pa_ipc.open_file(str(out))
    assert set(reader.schema.names) == {"timestamp_ns", "channel", "payload_bytes"}
