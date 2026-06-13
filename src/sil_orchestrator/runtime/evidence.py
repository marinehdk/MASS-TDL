from __future__ import annotations

import json
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo


def write_runtime_evidence(runs_dir: Path, payload: dict[str, object]) -> Path:
    runs_dir.mkdir(parents=True, exist_ok=True)
    now = datetime.now(ZoneInfo("Asia/Shanghai"))
    payload = {"timestamp": now.isoformat(), **payload}
    path = runs_dir / f"runtime_probe_{now.strftime('%Y%m%d_%H%M%S')}.json"
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return path
