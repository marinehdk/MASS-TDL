import subprocess

import pytest

from sil_orchestrator.runtime.compose import (
    ComposeRuntime,
    ComposeRuntimeError,
    DockerEngineClient,
    _decode_http_body,
)


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


class FakeEngine:
    def __init__(self):
        self.restarted = []
        self.started = []
        self.stopped = []
        self.timeouts = []
        self.containers = [
            {
                "Id": "abc123",
                "Names": ["/mass-l3-sil-sil-orchestrator-1"],
                "Image": "mass-l3-sil-sil-orchestrator",
                "State": "running",
                "Status": "Up 10 seconds (healthy)",
                "Labels": {
                    "com.docker.compose.project": "mass-l3-sil",
                    "com.docker.compose.service": "sil-orchestrator",
                },
            }
        ]

    def ps(self, project_name, timeout_s=10.0):
        assert project_name == "mass-l3-sil"
        self.timeouts.append(timeout_s)
        return [
            {
                "Service": "sil-orchestrator",
                "Name": "mass-l3-sil-sil-orchestrator-1",
                "Image": "mass-l3-sil-sil-orchestrator",
                "State": "running",
                "Health": "healthy",
                "Labels": {
                    "com.docker.compose.project": "mass-l3-sil",
                    "com.docker.compose.service": "sil-orchestrator",
                },
            }
        ]

    def restart_service(self, project_name, service, timeout_s=30.0):
        assert project_name == "mass-l3-sil"
        self.timeouts.append(timeout_s)
        self.restarted.append(service)

    def start_service(self, project_name, service, timeout_s=30.0):
        assert project_name == "mass-l3-sil"
        self.timeouts.append(timeout_s)
        self.started.append(service)

    def stop_service(self, project_name, service, timeout_s=30.0):
        assert project_name == "mass-l3-sil"
        self.timeouts.append(timeout_s)
        self.stopped.append(service)


class StaticEngineResponse:
    def __init__(self, response):
        self.response = response

    def _request_json(self, method, path, timeout_s):
        assert method == "GET"
        assert path == "/containers/json?all=true"
        assert timeout_s == 2.5
        return self.response


class MissingDockerRunner(FakeRunner):
    def __call__(self, command, timeout_s):
        self.calls.append((command, timeout_s))
        raise FileNotFoundError("docker")


class TimeoutSocket:
    def __init__(self):
        self.timeout = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False

    def settimeout(self, timeout):
        self.timeout = timeout

    def connect(self, socket_path):
        assert socket_path == "/var/run/docker.sock"

    def sendall(self, request):
        assert request.startswith(b"GET /containers/json?all=true")

    def recv(self, size):
        raise TimeoutError("timed out")


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


def test_ps_json_falls_back_to_docker_engine_when_cli_missing():
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml",),
        project_name="mass-l3-sil",
        runner=MissingDockerRunner(),
        engine=FakeEngine(),
    )

    rows = runtime.ps_json()

    assert '"Service": "sil-orchestrator"' in rows
    assert '"Health": "healthy"' in rows


def test_lifecycle_actions_fall_back_to_docker_engine_when_cli_missing():
    engine = FakeEngine()
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml",),
        project_name="mass-l3-sil",
        runner=MissingDockerRunner(),
        engine=engine,
    )

    runtime.restart_service("sil-orchestrator")
    runtime.start_service("plugin-route-l2-main")
    runtime.stop_plugin_service("plugin-route-l2-main")

    assert engine.restarted == ["sil-orchestrator"]
    assert engine.started == ["plugin-route-l2-main"]
    assert engine.stopped == ["plugin-route-l2-main"]
    assert engine.timeouts == [30.0, 30.0, 30.0]


def test_core_stack_restart_falls_back_to_engine_for_each_service():
    engine = FakeEngine()
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml",),
        project_name="mass-l3-sil",
        runner=MissingDockerRunner(),
        engine=engine,
    )

    runtime.restart_core_stack()

    assert engine.restarted == [
        "sil-orchestrator",
        "sil-nodes",
        "foxglove-bridge",
        "martin-tile-server",
    ]
    assert engine.timeouts == [60.0, 60.0, 60.0, 60.0]


def test_docker_engine_ps_maps_health_status_forms():
    engine = DockerEngineClient()
    engine._request_json = StaticEngineResponse(
        [
            {
                "Id": "healthy123",
                "Names": ["/mass-l3-sil-healthy-1"],
                "Image": "image-a",
                "State": "running",
                "Status": "Up 10 seconds (healthy)",
                "Labels": {
                    "com.docker.compose.project": "mass-l3-sil",
                    "com.docker.compose.service": "healthy-service",
                },
            },
            {
                "Id": "starting123",
                "Names": ["/mass-l3-sil-starting-1"],
                "Image": "image-b",
                "State": "running",
                "Status": "Up 10 seconds (health: starting)",
                "Labels": {
                    "com.docker.compose.project": "mass-l3-sil",
                    "com.docker.compose.service": "starting-service",
                },
            },
        ]
    )._request_json

    rows = {row["Service"]: row for row in engine.ps("mass-l3-sil", timeout_s=2.5)}

    assert rows["healthy-service"]["Health"] == "healthy"
    assert rows["starting-service"]["Health"] == "starting"


def test_docker_engine_request_uses_socket_timeout(monkeypatch):
    timeout_socket = TimeoutSocket()
    monkeypatch.setattr(
        "sil_orchestrator.runtime.compose.socket.socket",
        lambda *args: timeout_socket,
    )
    engine = DockerEngineClient()

    with pytest.raises(ComposeRuntimeError) as excinfo:
        engine.ps("mass-l3-sil", timeout_s=1.25)

    assert timeout_socket.timeout == 1.25
    assert "Docker Engine unavailable" in str(excinfo.value)


def test_docker_engine_chunked_response_body_is_decoded():
    header = b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked"
    body = b"5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n"

    assert _decode_http_body(header, body) == b"hello world"


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
