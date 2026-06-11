from __future__ import annotations

from dataclasses import dataclass
from typing import AsyncIterator, Protocol

from ais_twin.model import BBox, CanonicalAISRecord


@dataclass(frozen=True)
class ProviderStatus:
    connected: bool
    messages_received: int
    reconnects: int
    last_error: str | None


class AISProvider(Protocol):
    async def records(self, bbox: BBox) -> AsyncIterator[CanonicalAISRecord]:
        ...
