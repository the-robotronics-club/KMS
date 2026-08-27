"""Thread-safe script registry — tracks running script processes with PID."""
from __future__ import annotations

import logging
import threading
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Optional

logger = logging.getLogger(__name__)


@dataclass
class ScriptEntry:
    execution_id: uuid.UUID
    user_id: uuid.UUID
    script_name: str
    pid: int
    started_at: datetime
    process: object = None  # asyncio.subprocess.Process
    kill_event: threading.Event = field(default_factory=threading.Event)


class ScriptRegistry:
    """Thread-safe in-memory registry for running script processes."""

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._scripts: dict[uuid.UUID, ScriptEntry] = {}
        self._user_index: dict[uuid.UUID, set[uuid.UUID]] = {}  # user_id -> set of execution_ids

    def register(self, execution_id: uuid.UUID, user_id: uuid.UUID, script_name: str, pid: int, process: object = None) -> None:
        """Register a running script. Call after process is spawned."""
        with self._lock:
            entry = ScriptEntry(
                execution_id=execution_id,
                user_id=user_id,
                script_name=script_name,
                pid=pid,
                started_at=datetime.now(timezone.utc),
                process=process,
            )
            self._scripts[execution_id] = entry
            if user_id not in self._user_index:
                self._user_index[user_id] = set()
            self._user_index[user_id].add(execution_id)
            logger.debug(f"Registered script {script_name} (pid={pid}) as {execution_id}")

    def unregister(self, execution_id: uuid.UUID) -> Optional[ScriptEntry]:
        """Remove and return a script entry. Called when script completes."""
        with self._lock:
            entry = self._scripts.pop(execution_id, None)
            if entry:
                self._user_index.get(entry.user_id, set()).discard(execution_id)
                if not self._user_index.get(entry.user_id):
                    self._user_index.pop(entry.user_id, None)
            return entry

    def get(self, execution_id: uuid.UUID) -> Optional[ScriptEntry]:
        """Get a script entry by execution_id."""
        with self._lock:
            return self._scripts.get(execution_id)

    def get_user_scripts(self, user_id: uuid.UUID) -> list[ScriptEntry]:
        """Get all running scripts for a user."""
        with self._lock:
            ids = self._user_index.get(user_id, set())
            return [self._scripts[eid] for eid in ids if eid in self._scripts]

    def mark_kill_requested(self, execution_id: uuid.UUID) -> bool:
        """Signal that kill was requested. Returns True if entry existed."""
        with self._lock:
            entry = self._scripts.get(execution_id)
            if entry:
                entry.kill_event.set()
                return True
            return False

    def is_kill_requested(self, execution_id: uuid.UUID) -> bool:
        """Check if kill was requested for this execution."""
        with self._lock:
            entry = self._scripts.get(execution_id)
            return entry.kill_event.is_set() if entry else False


# Global singleton instance
script_registry = ScriptRegistry()