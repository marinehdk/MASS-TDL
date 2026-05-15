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
        runner = GateRunner(scenario_id="test-01")
        results = await runner.run_all()
        assert len(results) == 6
        assert all(r.passed for r in results)
    asyncio.run(_run())

def test_gate_label_for():
    runner = GateRunner(scenario_id="test-01")
    assert runner._gate_label_for(1) == "System Readiness"
    assert runner._gate_label_for(99) == "Gate 99"
