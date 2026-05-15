"""Phase 1 E1.8: M7 IEC 61508 watchdog monitor Python stub.

Mirrors the C++ WatchdogMonitor semantics:
  - startup grace period (no expiration during grace)
  - loss counting (consecutive missed heartbeats)
  - tolerance threshold (3 missed -> MRC trigger)
  - recovery (reset after healthy signal)

Source: V&V Plan §2.1 E1.8, SIF-2, architecture §11.
"""
from __future__ import annotations

import time
from dataclasses import dataclass, field


@dataclass
class WatchdogConfig:
    grace_period_s: float = 5.0
    heartbeat_interval_s: float = 1.0
    max_missed: int = 3
    recovery_healthy_count: int = 2


@dataclass
class MonitoredModule:
    module_id: str
    last_heartbeat: float = 0.0
    missed_count: int = 0
    healthy_count: int = 0
    mrc_triggered: bool = False
    in_grace: bool = True


class WatchdogMonitor:
    """M7 Safety Supervisor watchdog — monitors M1-M8 module heartbeats."""

    def __init__(self, config: WatchdogConfig | None = None):
        self.config = config or WatchdogConfig()
        self.modules: dict[str, MonitoredModule] = {}
        self.start_time = time.monotonic()
        self._initialized = False

    def register_module(self, module_id: str) -> MonitoredModule:
        mod = MonitoredModule(module_id=module_id)
        self.modules[module_id] = mod
        return mod

    def init_8_modules(self) -> None:
        """Register all 8 L3 modules: M1-M8."""
        if self._initialized:
            return
        for i in range(1, 9):
            self.register_module(f"M{i}")
        self._initialized = True

    def heartbeat(self, module_id: str) -> None:
        """Receive heartbeat from a module."""
        mod = self.modules.get(module_id)
        if mod is None:
            return
        now = time.monotonic()
        if mod.in_grace and (now - self.start_time) > self.config.grace_period_s:
            mod.in_grace = False
            mod.missed_count = 0
        mod.last_heartbeat = now
        mod.missed_count = 0
        mod.healthy_count += 1
        if mod.healthy_count >= self.config.recovery_healthy_count:
            mod.mrc_triggered = False

    def check_timeout(self, module_id: str) -> bool:
        """Check if module has timed out. Returns True if MRC should trigger."""
        mod = self.modules.get(module_id)
        if mod is None:
            return False
        if mod.in_grace:
            return False
        now = time.monotonic()
        elapsed = now - mod.last_heartbeat
        if elapsed > self.config.heartbeat_interval_s:
            mod.missed_count += 1
            mod.healthy_count = 0
            if mod.missed_count >= self.config.max_missed:
                mod.mrc_triggered = True
                return True
        return False

    def check_all(self) -> dict[str, bool]:
        """Check all modules. Returns {module_id: mrc_triggered}."""
        result: dict[str, bool] = {}
        for mid in self.modules:
            result[mid] = self.check_timeout(mid)
        return result

    def any_mrc(self) -> bool:
        """True if any module has triggered MRC."""
        return any(self.check_all().values())
