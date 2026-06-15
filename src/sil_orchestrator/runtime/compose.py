from __future__ import annotations

import json
import re
import socket
import subprocess
from collections.abc import Callable
from typing import Any


Runner = Callable[[list[str], float], subprocess.CompletedProcess[str]]


def run_command(command: list[str], timeout_s: float) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, timeout=timeout_s)


class ComposeRuntimeError(RuntimeError):
    """Raised when a compose lifecycle command fails."""


class DockerEngineClient:
    def __init__(self, socket_path: str = "/var/run/docker.sock") -> None:
        self.socket_path = socket_path

    def ps(self, project_name: str, timeout_s: float = 10.0) -> list[dict[str, Any]]:
        containers = self._request_json(
            "GET", "/containers/json?all=true", timeout_s=timeout_s
        )
        if not isinstance(containers, list):
            return []

        rows: list[dict[str, Any]] = []
        for container in containers:
            if not isinstance(container, dict):
                continue
            labels = _labels(container)
            if labels.get("com.docker.compose.project") != project_name:
                continue
            service = labels.get("com.docker.compose.service")
            if not service:
                continue
            rows.append(
                {
                    "Service": service,
                    "Name": _container_name(container),
                    "Image": str(container.get("Image") or ""),
                    "State": str(container.get("State") or ""),
                    "Health": _health_from_status(str(container.get("Status") or "")),
                    "Labels": labels,
                }
            )
        return rows

    def restart_service(
        self, project_name: str, service: str, timeout_s: float = 30.0
    ) -> None:
        container_id = self._container_id(project_name, service, timeout_s=timeout_s)
        self._request(
            "POST",
            f"/containers/{container_id}/restart?t=10",
            allowed={204},
            timeout_s=timeout_s,
        )

    def start_service(
        self, project_name: str, service: str, timeout_s: float = 30.0
    ) -> None:
        container_id = self._container_id(project_name, service, timeout_s=timeout_s)
        self._request(
            "POST",
            f"/containers/{container_id}/start",
            allowed={204, 304},
            timeout_s=timeout_s,
        )

    def stop_service(
        self, project_name: str, service: str, timeout_s: float = 30.0
    ) -> None:
        container_id = self._container_id(project_name, service, timeout_s=timeout_s)
        self._request(
            "POST",
            f"/containers/{container_id}/stop?t=10",
            allowed={204, 304},
            timeout_s=timeout_s,
        )

    def _container_id(
        self, project_name: str, service: str, timeout_s: float
    ) -> str:
        containers = self._request_json(
            "GET", "/containers/json?all=true", timeout_s=timeout_s
        )
        if not isinstance(containers, list):
            containers = []
        for container in containers:
            if not isinstance(container, dict):
                continue
            labels = _labels(container)
            if (
                labels.get("com.docker.compose.project") == project_name
                and labels.get("com.docker.compose.service") == service
            ):
                container_id = container.get("Id")
                if isinstance(container_id, str) and container_id:
                    return container_id
        raise ComposeRuntimeError(
            f"compose service {service!r} not found in project {project_name!r}"
        )

    def _request_json(self, method: str, path: str, timeout_s: float) -> Any:
        status, body = self._request(
            method, path, allowed={200}, timeout_s=timeout_s
        )
        if not body:
            return None
        try:
            return json.loads(body.decode("utf-8"))
        except json.JSONDecodeError as exc:
            raise ComposeRuntimeError(
                f"Docker Engine returned invalid JSON for {method} {path}"
            ) from exc

    def _request(
        self,
        method: str,
        path: str,
        allowed: set[int],
        timeout_s: float,
    ) -> tuple[int, bytes]:
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.settimeout(timeout_s)
                client.connect(self.socket_path)
                request = (
                    f"{method} {path} HTTP/1.1\r\n"
                    "Host: docker\r\n"
                    "Connection: close\r\n"
                    "Content-Length: 0\r\n"
                    "\r\n"
                ).encode("utf-8")
                client.sendall(request)
                response = b""
                while True:
                    chunk = client.recv(65536)
                    if not chunk:
                        break
                    response += chunk
        except OSError as exc:
            raise ComposeRuntimeError(f"Docker Engine unavailable: {exc}") from exc

        header, separator, body = response.partition(b"\r\n\r\n")
        if not separator:
            raise ComposeRuntimeError("Docker Engine returned malformed response")
        status_line = header.splitlines()[0].decode("iso-8859-1")
        parts = status_line.split()
        if len(parts) < 2 or not parts[1].isdigit():
            raise ComposeRuntimeError(
                f"Docker Engine returned malformed status: {status_line}"
            )
        status = int(parts[1])
        if status not in allowed:
            detail = _decode_http_body(header, body).decode(
                "utf-8", errors="replace"
            ).strip()
            raise ComposeRuntimeError(
                f"Docker Engine {method} {path} failed with HTTP {status}: {detail}"
            )
        return status, _decode_http_body(header, body)


class ComposeRuntime:
    def __init__(
        self,
        compose_files: tuple[str, ...],
        project_name: str,
        runner: Runner = run_command,
        engine: DockerEngineClient | None = None,
    ) -> None:
        self.compose_files = compose_files
        self.project_name = project_name
        self.runner = runner
        self.engine = engine or DockerEngineClient()

    def _base(self) -> list[str]:
        command = ["docker", "compose", "-p", self.project_name]
        for compose_file in self.compose_files:
            command.extend(["-f", compose_file])
        return command

    def _run(
        self, args: list[str], timeout_s: float = 30.0
    ) -> subprocess.CompletedProcess[str]:
        command = [*self._base(), *args]
        try:
            result = self.runner(command, timeout_s)
        except FileNotFoundError as exc:
            return self._run_with_engine(args, command, timeout_s, exc)
        except subprocess.TimeoutExpired as exc:
            raise ComposeRuntimeError(
                f"{' '.join(command)} timed out after {timeout_s:.1f}s"
            ) from exc
        if result.returncode != 0:
            detail = (result.stderr or result.stdout or "").strip()
            raise ComposeRuntimeError(f"{' '.join(command)} failed: {detail}")
        return result

    def _run_with_engine(
        self,
        args: list[str],
        command: list[str],
        timeout_s: float,
        cause: FileNotFoundError,
    ) -> subprocess.CompletedProcess[str]:
        try:
            if args == ["ps", "--format", "json"]:
                return subprocess.CompletedProcess(
                    command,
                    0,
                    stdout=json.dumps(self.engine.ps(self.project_name, timeout_s)),
                    stderr="",
                )
            if len(args) >= 2 and args[0] == "restart":
                for service in args[1:]:
                    self.engine.restart_service(self.project_name, service, timeout_s)
                return subprocess.CompletedProcess(command, 0, stdout="", stderr="")
            if len(args) >= 3 and args[:2] == ["up", "-d"]:
                for service in args[2:]:
                    self.engine.start_service(self.project_name, service, timeout_s)
                return subprocess.CompletedProcess(command, 0, stdout="", stderr="")
            if len(args) >= 2 and args[0] == "stop":
                for service in args[1:]:
                    self.engine.stop_service(self.project_name, service, timeout_s)
                return subprocess.CompletedProcess(command, 0, stdout="", stderr="")
        except ComposeRuntimeError:
            raise
        raise ComposeRuntimeError(
            f"{' '.join(command)} failed: missing docker CLI"
        ) from cause

    def ps_json(self) -> str:
        return self._run(["ps", "--format", "json"], timeout_s=10.0).stdout

    def start_service(self, service: str) -> None:
        self._run(["up", "-d", service])

    def restart_service(self, service: str) -> None:
        self._run(["restart", service])

    def stop_plugin_service(self, service: str) -> None:
        self._run(["stop", service])

    def start_core_stack(self) -> None:
        self._run(
            [
                "up",
                "-d",
                "sil-orchestrator",
                "sil-nodes",
                "foxglove-bridge",
                "martin-tile-server",
            ],
            timeout_s=60.0,
        )

    def restart_core_stack(self) -> None:
        self._run(
            [
                "restart",
                "sil-orchestrator",
                "sil-nodes",
                "foxglove-bridge",
                "martin-tile-server",
            ],
            timeout_s=60.0,
        )

    def stop_core_stack(self) -> None:
        self._run(
            [
                "stop",
                "sil-orchestrator",
                "sil-nodes",
                "foxglove-bridge",
                "martin-tile-server",
            ],
            timeout_s=60.0,
        )

    def switch_plugin(self, old_service: str | None, new_service: str) -> None:
        if old_service:
            self.stop_plugin_service(old_service)
        self.start_service(new_service)


def _labels(container: dict[str, Any]) -> dict[str, str]:
    labels = container.get("Labels")
    if not isinstance(labels, dict):
        return {}
    return {str(key): str(value) for key, value in labels.items() if value is not None}


def _container_name(container: dict[str, Any]) -> str:
    names = container.get("Names")
    if isinstance(names, list) and names:
        return str(names[0]).lstrip("/")
    return str(container.get("Id") or "")


def _health_from_status(status: str) -> str:
    match = re.search(r"\(health: ([^)]+)\)", status)
    if match:
        return match.group(1)
    match = re.search(r"\((healthy|unhealthy|starting)\)", status)
    return match.group(1) if match else ""


def _decode_http_body(header: bytes, body: bytes) -> bytes:
    if b"transfer-encoding: chunked" not in header.lower():
        return body

    decoded = bytearray()
    index = 0
    while True:
        line_end = body.find(b"\r\n", index)
        if line_end < 0:
            raise ComposeRuntimeError("Docker Engine returned malformed chunked body")
        size_text = body[index:line_end].split(b";", 1)[0]
        try:
            size = int(size_text, 16)
        except ValueError as exc:
            raise ComposeRuntimeError(
                "Docker Engine returned malformed chunk size"
            ) from exc
        index = line_end + 2
        if size == 0:
            return bytes(decoded)
        chunk = body[index : index + size]
        if len(chunk) != size:
            raise ComposeRuntimeError("Docker Engine returned truncated chunk")
        decoded.extend(chunk)
        index += size
        if body[index : index + 2] != b"\r\n":
            raise ComposeRuntimeError("Docker Engine returned malformed chunk ending")
        index += 2
