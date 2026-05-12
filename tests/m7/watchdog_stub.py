"""Python stub of IEC 61508 WatchdogMonitor (watchdog_monitor.hpp).

Mirrors mass_l3::m7::iec61508::WatchdogMonitor semantics exactly:
  - Before any on_message_received: startup grace (loss_count=0, heartbeat_ok=True).
  - evaluate(now_ms) increments loss_count[i] when now_ms - last_seen[i] > timeout_ms[i].
  - heartbeat_ok[i] is False when loss_count[i] STRICTLY GREATER than tolerance_count[i].
  - on_message_received(mod, now_ms) resets loss_count[mod] to 0 and records last_seen.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum


class MonitoredModule(IntEnum):
    M1 = 0
    M2 = 1
    M3 = 2
    M4 = 3
    M5 = 4
    M6 = 5
    M8 = 6
    COUNT = 7  # sentinel — must remain last


_N = int(MonitoredModule.COUNT)


@dataclass
class WatchdogConfig:
    timeout_ms: list[float] = field(
        default_factory=lambda: [1500.0, 300.0, 7500.0, 750.0, 1000.0, 750.0, 150.0]
    )
    tolerance_count: list[int] = field(
        default_factory=lambda: [3, 3, 2, 3, 3, 3, 3]
    )

    @classmethod
    def make_default(cls) -> "WatchdogConfig":
        return cls()


@dataclass
class WatchdogResult:
    heartbeat_ok: list[bool] = field(default_factory=lambda: [True] * _N)
    loss_count: list[int] = field(default_factory=lambda: [0] * _N)
    any_critical: bool = False
    critical_count: int = 0


class WatchdogMonitor:
    def __init__(self, cfg: WatchdogConfig) -> None:
        self._cfg = cfg
        self._last_seen: list[float | None] = [None] * _N
        self._loss_count: list[int] = [0] * _N

    def on_message_received(self, mod: MonitoredModule, now_ms: float) -> None:
        idx = int(mod)
        self._last_seen[idx] = now_ms
        self._loss_count[idx] = 0

    def evaluate(self, now_ms: float) -> WatchdogResult:
        hb_ok: list[bool] = []
        loss: list[int] = []
        for i in range(_N):
            last = self._last_seen[i]
            if last is None:
                # Startup grace: no message received → no loss
                hb_ok.append(True)
                loss.append(0)
            else:
                if now_ms - last > self._cfg.timeout_ms[i]:
                    self._loss_count[i] += 1
                else:
                    self._loss_count[i] = 0
                loss.append(self._loss_count[i])
                hb_ok.append(self._loss_count[i] <= self._cfg.tolerance_count[i])
        n_critical = sum(1 for ok in hb_ok if not ok)
        return WatchdogResult(
            heartbeat_ok=hb_ok,
            loss_count=loss,
            any_critical=n_critical > 0,
            critical_count=n_critical,
        )