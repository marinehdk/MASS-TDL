import subprocess

import pytest

from sil_orchestrator.runtime.compose import ComposeRuntime, ComposeRuntimeError


class FakeRunner:
    def __init__(self, result=None, error=None):
        self.calls = []
        self.result = result
        self.error = error

    def __call__(self, command, timeout_s):
        self.calls.append((command, timeout_s))
        if self.error:
            raise self.error
        if command[-3:] == ["ps", "--format", "json"]:
            return subprocess.CompletedProcess(command, 0, stdout="[]", stderr="")
        if self.result is not None:
            return self.result
        return subprocess.CompletedProcess(command, 0, stdout="ok", stderr="")


def test_compose_uses_configured_files_and_project_name():
    runner = FakeRunner()
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml", "docker-compose.plugins.yml"),
        project_name="mass-l3-sil",
        runner=runner,
    )

    runtime.restart_service("sil-orchestrator")

    command, timeout_s = runner.calls[0]
    assert command[:7] == [
        "docker",
        "compose",
        "-p",
        "mass-l3-sil",
        "-f",
        "docker-compose.yml",
        "-f",
    ]
    assert command[-2:] == ["restart", "sil-orchestrator"]
    assert timeout_s == 30.0


def test_stop_core_service_is_not_available_in_adapter():
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml",),
        project_name="mass-l3-sil",
        runner=FakeRunner(),
    )

    assert not hasattr(runtime, "stop_core_service")


def test_plugin_switch_command_sequence():
    runner = FakeRunner()
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml", "docker-compose.plugins.yml"),
        project_name="mass-l3-sil",
        runner=runner,
    )

    runtime.switch_plugin(old_service="plugin-route-l2-main", new_service="plugin-route-tdl-mock")

    commands = [call[0] for call in runner.calls]
    assert commands[0][-2:] == ["stop", "plugin-route-l2-main"]
    assert commands[1][-3:] == ["up", "-d", "plugin-route-tdl-mock"]


def test_ps_json_uses_ps_json_command_and_returns_stdout():
    runner = FakeRunner()
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml",),
        project_name="mass-l3-sil",
        runner=runner,
    )

    assert runtime.ps_json() == "[]"

    command, timeout_s = runner.calls[0]
    assert command[-3:] == ["ps", "--format", "json"]
    assert timeout_s == 10.0


@pytest.mark.parametrize(
    ("stdout", "stderr", "detail"),
    [
        ("ignored stdout", "compose stderr", "compose stderr"),
        ("compose stdout", "", "compose stdout"),
    ],
)
def test_nonzero_return_code_raises_compose_runtime_error(stdout, stderr, detail):
    runner = FakeRunner(
        result=subprocess.CompletedProcess(
            ["docker", "compose"],
            1,
            stdout=stdout,
            stderr=stderr,
        )
    )
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml",),
        project_name="mass-l3-sil",
        runner=runner,
    )

    with pytest.raises(ComposeRuntimeError) as excinfo:
        runtime.restart_service("sil-orchestrator")

    message = str(excinfo.value)
    assert "docker compose" in message
    assert "restart sil-orchestrator" in message
    assert detail in message


def test_timeout_raises_compose_runtime_error():
    timeout = subprocess.TimeoutExpired(["docker", "compose"], 30.0)
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml",),
        project_name="mass-l3-sil",
        runner=FakeRunner(error=timeout),
    )

    with pytest.raises(ComposeRuntimeError) as excinfo:
        runtime.restart_service("sil-orchestrator")

    message = str(excinfo.value)
    assert "docker compose" in message
    assert "restart sil-orchestrator" in message
    assert "timed out" in message
    assert excinfo.value.__cause__ is timeout


@pytest.mark.parametrize(
    ("method_name", "expected_args"),
    [
        (
            "start_core_stack",
            [
                "up",
                "-d",
                "sil-orchestrator",
                "sil-nodes",
                "foxglove-bridge",
                "martin-tile-server",
            ],
        ),
        (
            "restart_core_stack",
            [
                "restart",
                "sil-orchestrator",
                "sil-nodes",
                "foxglove-bridge",
                "martin-tile-server",
            ],
        ),
        (
            "stop_core_stack",
            [
                "stop",
                "sil-orchestrator",
                "sil-nodes",
                "foxglove-bridge",
                "martin-tile-server",
            ],
        ),
    ],
)
def test_core_stack_commands_use_expected_services_and_timeout(method_name, expected_args):
    runner = FakeRunner()
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml",),
        project_name="mass-l3-sil",
        runner=runner,
    )

    getattr(runtime, method_name)()

    command, timeout_s = runner.calls[0]
    assert command[-len(expected_args) :] == expected_args
    assert timeout_s == 60.0


def test_plugin_switch_without_old_service_only_starts_new_service():
    runner = FakeRunner()
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml", "docker-compose.plugins.yml"),
        project_name="mass-l3-sil",
        runner=runner,
    )

    runtime.switch_plugin(old_service=None, new_service="plugin-route-tdl-mock")

    commands = [call[0] for call in runner.calls]
    assert len(commands) == 1
    assert commands[0][-3:] == ["up", "-d", "plugin-route-tdl-mock"]
