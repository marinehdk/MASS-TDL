from __future__ import annotations

from pathlib import Path
from typing import Any

import pyarrow as pa
import pyarrow.ipc as ipc


class ArrowWriter:
    """Side-channel Arrow IPC file writer for scoring data with polars bridge.

    Writes RecordBatch files using PyArrow's streaming IPC format.  Compatible
    with polars.read_ipc() for downstream analytics.
    """

    SCHEMA = pa.schema([
        pa.field("stamp", pa.float64()),
        pa.field("safety", pa.float64()),
        pa.field("rule_compliance", pa.float64()),
        pa.field("delay_penalty", pa.float64()),
        pa.field("action_magnitude_penalty", pa.float64()),
        pa.field("phase_score", pa.float64()),
        pa.field("plausibility", pa.float64()),
        pa.field("total", pa.float64()),
        pa.field("cpa_nm", pa.float64()),
        pa.field("cpa_target_nm", pa.float64()),
    ])

    def __init__(self, output_path: str, batch_size: int = 60) -> None:
        self._path = Path(output_path)
        self._path.parent.mkdir(parents=True, exist_ok=True)
        self._batch_size = batch_size
        self._buffer: list[dict[str, Any]] = []
        self._writer: ipc.RecordBatchFileWriter | None = None

    def _ensure_writer_open(self) -> None:
        if self._writer is None:
            sink = pa.OSFile(str(self._path), "wb")
            self._writer = ipc.new_file(sink, self.SCHEMA)

    def append(self, row: Any) -> None:
        if hasattr(row, "__dataclass_fields__"):
            d = {f: getattr(row, f) for f in row.__dataclass_fields__}
        else:
            d = row
        self._buffer.append(d)
        if len(self._buffer) >= self._batch_size:
            self.flush()

    def flush(self) -> None:
        if not self._buffer:
            return
        self._ensure_writer_open()
        batch = pa.RecordBatch.from_pylist(self._buffer, schema=self.SCHEMA)
        self._writer.write_batch(batch)
        self._buffer.clear()

    def close(self) -> None:
        self.flush()
        if self._writer is not None:
            self._writer.close()
            self._writer = None

    def __enter__(self) -> ArrowWriter:
        return self

    def __exit__(self, *args: Any) -> None:
        self.close()
