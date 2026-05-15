"""测试 KpiDeriver — 从 scoring.arrow 派生 8 项 KPI。"""

import tempfile
from pathlib import Path

import pyarrow as pa
import pyarrow.ipc as ipc
import pytest
from scoring.kpi_deriver import KpiDeriver


def _write_test_arrow(path: str, rows: list[dict]) -> None:
    """将字典行列表写入 Arrow IPC 文件。"""
    schema = pa.schema(
        [
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
        ]
    )
    with pa.OSFile(path, "wb") as sink:
        writer = ipc.new_file(sink, schema)
        batch = pa.RecordBatch.from_pylist(rows, schema=schema)
        writer.write_batch(batch)
        writer.close()


class TestKpiDeriver:
    """测试 KpiDeriver.derive_from_arrow()。"""

    def test_kpi_from_scoring_arrow(self) -> None:
        """从 3 行 scoring 数据中派生 min_cpa、tcpa 和 decision_count。"""
        with tempfile.TemporaryDirectory() as tmpdir:
            arrow_path = Path(tmpdir) / "scoring.arrow"
            rows = [
                {
                    "stamp": 0.0,
                    "safety": 1.0,
                    "rule_compliance": 1.0,
                    "delay_penalty": 0.0,
                    "action_magnitude_penalty": 0.0,
                    "phase_score": 1.0,
                    "plausibility": 1.0,
                    "total": 1.0,
                    "cpa_nm": 1.0,
                    "cpa_target_nm": 0.27,
                },
                {
                    "stamp": 100.0,
                    "safety": 0.5,
                    "rule_compliance": 1.0,
                    "delay_penalty": 0.1,
                    "action_magnitude_penalty": 0.0,
                    "phase_score": 1.0,
                    "plausibility": 1.0,
                    "total": 0.85,
                    "cpa_nm": 0.14,
                    "cpa_target_nm": 0.27,
                },
                {
                    "stamp": 200.0,
                    "safety": 0.8,
                    "rule_compliance": 0.8,
                    "delay_penalty": 0.0,
                    "action_magnitude_penalty": 0.05,
                    "phase_score": 0.5,
                    "plausibility": 1.0,
                    "total": 0.72,
                    "cpa_nm": 0.35,
                    "cpa_target_nm": 0.27,
                },
            ]
            _write_test_arrow(str(arrow_path), rows)

            deriver = KpiDeriver()
            kpis = deriver.derive_from_arrow(str(arrow_path))

            # 第二行 cpa_nm=0.14 最小
            assert abs(kpis["min_cpa_nm"] - 0.14) < 0.01
            # tcpa_min_s 应为 cpa 最小行的 stamp
            assert abs(kpis["tcpa_min_s"] - 100.0) < 1.0
            # 共 3 个决策
            assert kpis["decision_count"] == 3

    def test_kpi_from_scoring_arrow_with_own_ship(self) -> None:
        """包含 own_ship 数据文件时，派生 rot、rudder、deviation 等 KPI。"""
        with tempfile.TemporaryDirectory() as tmpdir:
            scoring_path = str(Path(tmpdir) / "scoring.arrow")
            own_path = str(Path(tmpdir) / "own_ship.arrow")

            _write_test_arrow(
                scoring_path,
                [
                    {
                        "stamp": 0.0,
                        "safety": 1.0,
                        "rule_compliance": 1.0,
                        "delay_penalty": 0.0,
                        "action_magnitude_penalty": 0.0,
                        "phase_score": 1.0,
                        "plausibility": 1.0,
                        "total": 1.0,
                        "cpa_nm": 0.5,
                        "cpa_target_nm": 0.27,
                    },
                ],
            )

            # 构造 own_ship Arrow 文件
            own_schema = pa.schema(
                [
                    pa.field("stamp", pa.float64()),
                    pa.field("rot_dps", pa.float64()),
                    pa.field("rudder_angle_deg", pa.float64()),
                    pa.field("cross_track_error_nm", pa.float64()),
                ]
            )
            own_rows = [
                {"stamp": 0.0, "rot_dps": -3.0, "rudder_angle_deg": 5.0, "cross_track_error_nm": 0.002},
                {"stamp": 1.0, "rot_dps": 2.0, "rudder_angle_deg": -10.0, "cross_track_error_nm": -0.001},
                {"stamp": 2.0, "rot_dps": -1.0, "rudder_angle_deg": 3.0, "cross_track_error_nm": 0.0},
            ]
            with pa.OSFile(own_path, "wb") as sink:
                writer = ipc.new_file(sink, own_schema)
                writer.write_batch(pa.RecordBatch.from_pylist(own_rows, schema=own_schema))
                writer.close()

            deriver = KpiDeriver()
            kpis = deriver.derive_from_arrow(
                scoring_arrow_path=scoring_path,
                own_ship_arrow_path=own_path,
                grounding_risk=0.15,
            )

            # avg_rot_dpm = mean(abs(rot_dps)) * 60 → (3+2+1)/3 * 60 = 120.0
            assert abs(kpis["avg_rot_dpm"] - 120.0) < 1.0
            # max_rudder_deg = max(abs(rudder_angle_deg)) = 10.0
            assert abs(kpis["max_rudder_deg"] - 10.0) < 0.1
            # grounding_risk_score = 0.15
            assert abs(kpis["grounding_risk_score"] - 0.15) < 0.001
            # route_deviation_nm = max(abs(cross_track_error_nm)) = 0.002
            assert abs(kpis["route_deviation_nm"] - 0.002) < 1e-6

    def test_kpi_empty_arrow(self) -> None:
        """空 Arrow 文件应返回零值和默认 risk。"""
        with tempfile.TemporaryDirectory() as tmpdir:
            empty_path = str(Path(tmpdir) / "empty.arrow")
            # 创建与 scoring 相同 schema 的空文件
            schema = pa.schema(
                [
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
                ]
            )
            with pa.OSFile(empty_path, "wb") as sink:
                writer = ipc.new_file(sink, schema)
                writer.close()

            deriver = KpiDeriver()
            kpis = deriver.derive_from_arrow(empty_path)

            assert kpis["min_cpa_nm"] == 0.0
            assert kpis["tcpa_min_s"] == 0.0
            assert kpis["decision_count"] == 0
            assert kpis["grounding_risk_score"] == 1.0
