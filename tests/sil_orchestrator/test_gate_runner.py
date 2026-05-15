import asyncio
import yaml
from sil_orchestrator.gate_runner import GateResult, GateRunner, GateSpec

def test_gate_result_fields():
    r = GateResult(gate_id=1, passed=True, checks=["docker:ok", "dds:ok"], duration_ms=42.5, rationale="all green")
    assert r.gate_id == 1
    assert r.passed == True
    assert r.go_no_go() == True
    assert len(r.checks) == 2

def test_gate_result_go_no_go_all_pass():
    r = GateResult(gate_id=1, passed=True, checks=["a"], duration_ms=10, rationale="ok")
    assert r.go_no_go() == True

def test_gate_result_go_no_go_any_fail():
    r = GateResult(gate_id=1, passed=False, checks=["docker:failed"], duration_ms=10, rationale="docker down")
    assert r.go_no_go() == False

def test_gate_runner_registers_gates():
    runner = GateRunner(scenario_id="test-01")
    assert len(runner.gates) == 6
    assert runner.gates[0].gate_id == 1
    assert runner.gates[0].label == "System Readiness"

def test_gate_runner_run_all_stubs():
    async def _run():
        with patch("sil_orchestrator.gate_runner.gate_1_system_readiness", new_callable=AsyncMock) as mock_g1, \
             patch("sil_orchestrator.gate_runner.gate_2_module_health", new_callable=AsyncMock) as mock_g2, \
             patch("sil_orchestrator.gate_runner.gate_3_scenario_integrity", new_callable=AsyncMock) as mock_g3, \
             patch("sil_orchestrator.gate_runner.gate_4_odd_alignment", new_callable=AsyncMock) as mock_g4, \
             patch("sil_orchestrator.gate_runner.gate_5_time_base", new_callable=AsyncMock) as mock_g5:
            mock_g1.return_value = GateResult(gate_id=1, passed=True, checks=["ok"], duration_ms=0, rationale="ok")
            mock_g2.return_value = GateResult(gate_id=2, passed=True, checks=["ok"], duration_ms=0, rationale="ok")
            mock_g3.return_value = GateResult(gate_id=3, passed=True, checks=["ok"], duration_ms=0, rationale="ok")
            mock_g4.return_value = GateResult(gate_id=4, passed=True, checks=["ok"], duration_ms=0, rationale="ok")
            mock_g5.return_value = GateResult(gate_id=5, passed=True, checks=["ok"], duration_ms=0, rationale="ok")
            runner = GateRunner(scenario_id="test-01")
            results = await runner.run_all()
        assert len(results) == 6
        assert all(r.passed for r in results)
    asyncio.run(_run())

def test_gate_label_for():
    runner = GateRunner(scenario_id="test-01")
    assert runner._gate_label_for(1) == "System Readiness"
    assert runner._gate_label_for(99) == "Gate 99"


import pytest
from unittest.mock import AsyncMock, patch
from sil_orchestrator.gate_runner import gate_1_system_readiness

@pytest.mark.asyncio
async def test_gate_1_all_pass():
    """GATE 1: all 5 sub-checks pass -> PASS"""
    with patch("sil_orchestrator.gate_runner._check_docker_services", new_callable=AsyncMock) as mock_docker, \
         patch("sil_orchestrator.gate_runner._check_ros2_discovery", new_callable=AsyncMock) as mock_ros2, \
         patch("sil_orchestrator.gate_runner._check_foxglove_bridge", new_callable=AsyncMock) as mock_fox, \
         patch("sil_orchestrator.gate_runner._check_martin_tileserver", new_callable=AsyncMock) as mock_martin, \
         patch("sil_orchestrator.gate_runner._check_ws_connected", new_callable=AsyncMock) as mock_ws:
        mock_docker.return_value = ("ok", "docker compose 5/5 healthy")
        mock_ros2.return_value = ("ok", "ROS2 DDS discovery: 3/3 nodes visible")
        mock_fox.return_value = ("ok", "foxglove_bridge :8765 listening")
        mock_martin.return_value = ("ok", "martin :3000 responsive")
        mock_ws.return_value = ("ok", "telemetry WS connected")
        result = await gate_1_system_readiness()
        assert result.gate_id == 1
        assert result.passed == True
        assert len(result.checks) == 5

@pytest.mark.asyncio
async def test_gate_1_docker_fail():
    """GATE 1: docker unhealthy -> FAIL"""
    with patch("sil_orchestrator.gate_runner._check_docker_services", new_callable=AsyncMock) as mock_docker, \
         patch("sil_orchestrator.gate_runner._check_ros2_discovery", new_callable=AsyncMock) as mock_ros2, \
         patch("sil_orchestrator.gate_runner._check_foxglove_bridge", new_callable=AsyncMock) as mock_fox, \
         patch("sil_orchestrator.gate_runner._check_martin_tileserver", new_callable=AsyncMock) as mock_martin, \
         patch("sil_orchestrator.gate_runner._check_ws_connected", new_callable=AsyncMock) as mock_ws:
        mock_docker.return_value = ("fail", "docker compose: 3/5 healthy")
        mock_ros2.return_value = ("ok", "ROS2 DDS discovery ok")
        mock_fox.return_value = ("fail", "foxglove_bridge not listening")
        mock_martin.return_value = ("fail", "martin :3000 not responsive")
        mock_ws.return_value = ("ok", "telemetry WS connected")
        result = await gate_1_system_readiness()
        assert result.passed == False
        assert any("fail" in c for c in result.checks)


from sil_orchestrator.gate_runner import gate_2_module_health, ModulePulseCheck

FAKE_PULSES_ALL_GREEN = [
    ModulePulseCheck(module="M1", state=1, latency_ms=2, drops=0),
    ModulePulseCheck(module="M2", state=1, latency_ms=3, drops=0),
    ModulePulseCheck(module="M3", state=1, latency_ms=1, drops=0),
    ModulePulseCheck(module="M4", state=1, latency_ms=2, drops=0),
    ModulePulseCheck(module="M5", state=1, latency_ms=4, drops=0),
    ModulePulseCheck(module="M6", state=1, latency_ms=3, drops=0),
    ModulePulseCheck(module="M7", state=1, latency_ms=2, drops=0),
    ModulePulseCheck(module="M8", state=1, latency_ms=1, drops=0),
]

FAKE_PULSES_M3_RED = [
    ModulePulseCheck(module="M1", state=1, latency_ms=2, drops=0),
    ModulePulseCheck(module="M2", state=1, latency_ms=3, drops=0),
    ModulePulseCheck(module="M3", state=3, latency_ms=120, drops=5),
    ModulePulseCheck(module="M4", state=1, latency_ms=2, drops=0),
    ModulePulseCheck(module="M5", state=1, latency_ms=4, drops=0),
    ModulePulseCheck(module="M6", state=1, latency_ms=3, drops=0),
    ModulePulseCheck(module="M7", state=1, latency_ms=2, drops=0),
    ModulePulseCheck(module="M8", state=1, latency_ms=1, drops=0),
]


@pytest.mark.asyncio
async def test_gate_2_all_green():
    """GATE 2: 8/8 GREEN + M7 independent -> PASS"""
    with patch("sil_orchestrator.gate_runner._fetch_module_pulses", return_value=FAKE_PULSES_ALL_GREEN), \
         patch("sil_orchestrator.gate_runner._verify_m7_independent", new_callable=AsyncMock) as mock_m7:
        mock_m7.return_value = ("ok", "M7 PID 12345 independent from component_container")
        result = await gate_2_module_health()
        assert result.gate_id == 2
        assert result.passed == True
        assert len(result.checks) == 9


@pytest.mark.asyncio
async def test_gate_2_m3_red_fails():
    """GATE 2: M3 RED -> FAIL"""
    with patch("sil_orchestrator.gate_runner._fetch_module_pulses", return_value=FAKE_PULSES_M3_RED), \
         patch("sil_orchestrator.gate_runner._verify_m7_independent", new_callable=AsyncMock) as mock_m7:
        mock_m7.return_value = ("ok", "M7 PID independent")
        result = await gate_2_module_health()
        assert result.passed == False
        assert any("M3" in c and "RED" in c for c in result.checks)


@pytest.mark.asyncio
async def test_gate_2_m7_not_independent():
    """GATE 2: M7 process not independent -> FAIL"""
    with patch("sil_orchestrator.gate_runner._fetch_module_pulses", return_value=FAKE_PULSES_ALL_GREEN), \
         patch("sil_orchestrator.gate_runner._verify_m7_independent", new_callable=AsyncMock) as mock_m7:
        mock_m7.return_value = ("fail", "M7 PID 12345 found inside component_container — Doer-Checker isolation violated")
        result = await gate_2_module_health()
        assert result.passed == False


from sil_orchestrator.gate_runner import gate_3_scenario_integrity
import hashlib

FAKE_SCENARIO_VALID = {
    "yaml_content": "name: test\nversion: 1.0\n",
    "hash": "",  # computed below
}
FAKE_SCENARIO_VALID["hash"] = hashlib.sha256(FAKE_SCENARIO_VALID["yaml_content"].encode()).hexdigest()

SCENARIO_WITH_EXPECTED_OUTCOME = {
    "yaml_content": "name: test\nversion: 1.0\nexpected_outcome:\n  cpa_min_m_ge: 0.5\n  colregs_rules: [14]\n  grounding: forbidden\n",
    "hash": "",
}
SCENARIO_WITH_EXPECTED_OUTCOME["hash"] = hashlib.sha256(SCENARIO_WITH_EXPECTED_OUTCOME["yaml_content"].encode()).hexdigest()

@pytest.mark.asyncio
async def test_gate_3_hash_match():
    """GATE 3: hash match -> PASS"""
    data = {"yaml_content": FAKE_SCENARIO_VALID["yaml_content"], "hash": FAKE_SCENARIO_VALID["hash"]}
    with patch("sil_orchestrator.gate_runner.yaml") as mock_yaml:
        mock_yaml.safe_load.return_value = {"name": "test", "version": "1.0", "expected_outcome": {"cpa_min_m_ge": 0.5}}
        mock_yaml.YAMLError = yaml.YAMLError
        result = await gate_3_scenario_integrity("test-01", data)
        assert result.gate_id == 3
        assert result.passed == True
        assert any("hash" in c.lower() and "ok" in c.lower() for c in result.checks)

@pytest.mark.asyncio
async def test_gate_3_hash_mismatch():
    """GATE 3: hash mismatch -> FAIL"""
    data = {"yaml_content": FAKE_SCENARIO_VALID["yaml_content"], "hash": "wrong_hash_000"}
    with patch("sil_orchestrator.gate_runner.yaml") as mock_yaml:
        mock_yaml.safe_load.return_value = {"name": "test"}
        mock_yaml.YAMLError = yaml.YAMLError
        result = await gate_3_scenario_integrity("test-01", data)
        assert result.passed == False
        assert any("hash" in c.lower() and "fail" in c.lower() for c in result.checks)

@pytest.mark.asyncio
async def test_gate_3_no_expected_outcome():
    """GATE 3: YAML without expected_outcome -> FAIL"""
    data = {"yaml_content": "name: bare\n", "hash": hashlib.sha256(b"name: bare\n").hexdigest()}
    with patch("sil_orchestrator.gate_runner.yaml") as mock_yaml:
        mock_yaml.safe_load.return_value = {"name": "bare"}
        mock_yaml.YAMLError = yaml.YAMLError
        result = await gate_3_scenario_integrity("test-01", data)
        assert result.passed == False
        assert any("expected_outcome" in c.lower() for c in result.checks)

@pytest.mark.asyncio
async def test_gate_3_no_scenario_data():
    """GATE 3: scenario_data=None -> FAIL gracefully"""
    result = await gate_3_scenario_integrity("unknown", None)
    assert result.passed == False
    assert any("not found" in c.lower() for c in result.checks)


from sil_orchestrator.gate_runner import gate_4_odd_alignment

SCENARIO_WITH_ODD = """
name: test
version: "1.0"
metadata:
  odd_cell:
    domain: open_sea
    visibility_nm: 5.0
    sea_state_beaufort: 3
    max_wind_kn: 25
"""

SCENARIO_WITHOUT_ODD = """
name: test_no_odd
version: "1.0"
"""

SCENARIO_ODD_OUT_OF_BOUNDS = """
name: test
version: "1.0"
metadata:
  odd_cell:
    domain: open_sea
    visibility_nm: 0.05
    sea_state_beaufort: 10
    max_wind_kn: 60
"""

@pytest.mark.asyncio
async def test_gate_4_odd_match():
    """GATE 4: odd_cell within bounds -> PASS"""
    data = {"yaml_content": SCENARIO_WITH_ODD}
    result = await gate_4_odd_alignment("test-01", data)
    assert result.gate_id == 4
    assert result.passed == True

@pytest.mark.asyncio
async def test_gate_4_no_odd_graceful():
    """GATE 4: no metadata.odd_cell -> PASS with warning (Phase 1)"""
    data = {"yaml_content": SCENARIO_WITHOUT_ODD}
    result = await gate_4_odd_alignment("test-01", data)
    assert result.passed == True
    assert any("no odd_cell" in c.lower() for c in result.checks)

@pytest.mark.asyncio
async def test_gate_4_odd_out_of_bounds():
    """GATE 4: visibility < 0.1 -> FAIL"""
    data = {"yaml_content": SCENARIO_ODD_OUT_OF_BOUNDS}
    result = await gate_4_odd_alignment("test-01", data)
    assert result.passed == False
    assert any("visibility" in c.lower() for c in result.checks)


from sil_orchestrator.gate_runner import gate_5_time_base

@pytest.mark.asyncio
async def test_gate_5_all_pass():
    """GATE 5: all 4 checks pass -> PASS"""
    with patch("sil_orchestrator.gate_runner._check_utc_ptp", new_callable=AsyncMock) as mock_utc, \
         patch("sil_orchestrator.gate_runner._check_sim_clock", new_callable=AsyncMock) as mock_clock, \
         patch("sil_orchestrator.gate_runner._check_rosbag2_ready", new_callable=AsyncMock) as mock_bag, \
         patch("sil_orchestrator.gate_runner._check_asdr_ready", new_callable=AsyncMock) as mock_asdr:
        mock_utc.return_value = ("ok", "PTP offset 0.5ms < 10ms")
        mock_clock.return_value = ("ok", "/clock publishing at 50Hz")
        mock_bag.return_value = ("ok", "rosbag2 recorder running")
        mock_asdr.return_value = ("ok", "ASDR /runs/ directory writable")
        result = await gate_5_time_base("test-01")
        assert result.gate_id == 5
        assert result.passed == True
        assert len(result.checks) == 4

@pytest.mark.asyncio
async def test_gate_5_utc_drift_fail():
    """GATE 5: PTP drift > 10ms -> FAIL"""
    with patch("sil_orchestrator.gate_runner._check_utc_ptp", new_callable=AsyncMock) as mock_utc, \
         patch("sil_orchestrator.gate_runner._check_sim_clock", new_callable=AsyncMock) as mock_clock, \
         patch("sil_orchestrator.gate_runner._check_rosbag2_ready", new_callable=AsyncMock) as mock_bag, \
         patch("sil_orchestrator.gate_runner._check_asdr_ready", new_callable=AsyncMock) as mock_asdr:
        mock_utc.return_value = ("fail", "PTP offset 45ms > 10ms threshold")
        mock_clock.return_value = ("ok", "/clock publishing")
        mock_bag.return_value = ("ok", "rosbag2 ready")
        mock_asdr.return_value = ("ok", "ASDR ready")
        result = await gate_5_time_base("test-01")
        assert result.passed == False
