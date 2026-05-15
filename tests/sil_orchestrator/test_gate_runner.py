import asyncio
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
