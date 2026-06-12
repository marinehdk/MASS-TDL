import subprocess

from sil_orchestrator.runtime.compose import ComposeRuntime


class FakeRunner:
    def __init__(self):
        self.calls = []

    def __call__(self, command, timeout_s):
        self.calls.append((command, timeout_s))
        if command[-1] == "ps":
            return subprocess.CompletedProcess(command, 0, stdout="[]", stderr="")
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
