from __future__ import annotations

import subprocess
from collections.abc import Callable


Runner = Callable[[list[str], float], subprocess.CompletedProcess[str]]


def run_command(command: list[str], timeout_s: float) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, timeout=timeout_s)


class ComposeRuntimeError(RuntimeError):
    """Raised when a compose lifecycle command fails."""


class ComposeRuntime:
    def __init__(
        self,
        compose_files: tuple[str, ...],
        project_name: str,
        runner: Runner = run_command,
    ) -> None:
        self.compose_files = compose_files
        self.project_name = project_name
        self.runner = runner

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
        except subprocess.TimeoutExpired as exc:
            raise ComposeRuntimeError(
                f"{' '.join(command)} timed out after {timeout_s:.1f}s"
            ) from exc
        if result.returncode != 0:
            detail = (result.stderr or result.stdout or "").strip()
            raise ComposeRuntimeError(f"{' '.join(command)} failed: {detail}")
        return result

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
