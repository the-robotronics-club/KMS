"""Script runner router — run, stream, and kill admin ops scripts."""
from __future__ import annotations

import asyncio
import json
import logging
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Annotated, Optional, cast

from fastapi import APIRouter, Depends, HTTPException, Query
from fastapi.responses import StreamingResponse
from sqlalchemy import select

from app.api.deps import require_admin
from app.core.database import AsyncSessionLocal, get_db
from app.models.script_execution import ScriptExecution, ScriptStatus
from app.models.user import User
from app.schemas import (
    ScriptKillResponse,
    ScriptRunRequest,
    ScriptRunResponse,
    ScriptStatusOut,
)
from app.services.script_registry import script_registry

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/scripts", tags=["scripts"])

# Container has backend/ copied to /app/, so scripts land at /app/scripts/.
SCRIPTS_DIR = Path(__file__).resolve().parents[2] / "scripts"
KILL_GRACE_SECONDS = 5.0
OUTPUT_CAP = 64 * 1024  # keep last 64KB per run

# execution_id -> asyncio.Queue[str]; the runner task feeds it, the SSE
# endpoint drains it. Cleaned up on done/error.
_queues: dict[uuid.UUID, asyncio.Queue[Optional[str]]] = {}


def _resolve_script(script_name: str) -> Path:
    if not script_name or "/" in script_name or script_name.startswith("."):
        raise HTTPException(status_code=400, detail="Invalid script name")
    path = SCRIPTS_DIR / script_name
    if not path.is_file() or path.suffix != ".sh":
        raise HTTPException(
            status_code=404,
            detail=f"Unknown script '{script_name}' (available: {', '.join(sorted(p.name for p in SCRIPTS_DIR.glob('*.sh')))})",
        )
    return path


def _emit(queue: asyncio.Queue[Optional[str]], event: dict) -> None:
    queue.put_nowait(f"data: {json.dumps(event)}\n\n")


async def _finish(
    execution_id: uuid.UUID,
    status: ScriptStatus,
    exit_code: int | None,
    output: str,
    error: str | None,
) -> None:
    """Persist final state and close the SSE stream."""
    queue = _queues.pop(execution_id, None)
    if queue is not None:
        _emit(
            queue,
            {
                "type": "done",
                "execution_id": str(execution_id),
                "status": status.value,
                "exit_code": exit_code,
            },
        )
        queue.put_nowait(None)  # EOF marker
    async with AsyncSessionLocal() as db:
        row = await db.get(ScriptExecution, execution_id)
        if row is None:
            return
        row.status = status
        row.exit_code = exit_code
        row.finished_at = datetime.now(timezone.utc)
        row.output = output[-OUTPUT_CAP:] if output else None
        row.error = error
        await db.commit()


async def _run_task(
    execution_id: uuid.UUID,
    path: Path,
    queue: asyncio.Queue[str],
) -> None:
    """Spawn the process, stream stdout/stderr, then finalize."""
    entry = script_registry.get(execution_id)
    if entry is None or entry.process is None:
        return
    process = cast(asyncio.subprocess.Process, entry.process)

    output_chunks: list[str] = []
    try:
        assert process.stdout is not None and process.stderr is not None
        streams = [process.stdout, process.stderr]
        while streams:
            for stream in list(streams):
                line = await stream.readline()
                if not line:
                    streams.remove(stream)
                    continue
                chunk = line.decode(errors="replace")
                output_chunks.append(chunk)
                _emit(queue, {"type": "output", "execution_id": str(execution_id), "output": chunk})
        exit_code = await process.wait()
    except asyncio.CancelledError:
        raise
    except Exception as e:  # defensive: never let the task die silently
        logger.exception("script %s task failed", execution_id)
        await _finish(execution_id, ScriptStatus.failed, None, "".join(output_chunks), str(e))
        return

    # Kill path owns the final state; skip if a kill was requested.
    if script_registry.is_kill_requested(execution_id):
        return
    script_registry.unregister(execution_id)
    await _finish(
        execution_id,
        ScriptStatus.completed if exit_code == 0 else ScriptStatus.failed,
        exit_code,
        "".join(output_chunks),
        None,
    )


@router.post("", response_model=ScriptRunResponse)
async def run_script(
    req: ScriptRunRequest,
    user: Annotated[User, Depends(require_admin)],
    db=Depends(get_db),
) -> ScriptRunResponse:
    path = _resolve_script(req.script_name)
    execution_id = uuid.uuid4()

    row = ScriptExecution(id=execution_id, user_id=user.id, script_name=req.script_name)
    db.add(row)
    await db.commit()

    try:
        process = await asyncio.create_subprocess_exec(
            "bash", str(path),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
    except Exception as e:
        row.status = ScriptStatus.failed
        row.error = str(e)
        await db.commit()
        raise HTTPException(status_code=500, detail=f"Failed to start script: {e}")

    row.pid = process.pid
    await db.commit()
    script_registry.register(execution_id, user.id, req.script_name, process.pid, process)

    queue: asyncio.Queue[Optional[str]] = asyncio.Queue()
    _queues[execution_id] = queue
    asyncio.create_task(_run_task(execution_id, path, queue))

    return ScriptRunResponse(execution_id=execution_id, status=ScriptStatus.running)


@router.post("/{execution_id}/kill", response_model=ScriptKillResponse)
async def kill_script(
    execution_id: uuid.UUID,
    user: Annotated[User, Depends(require_admin)],
    db=Depends(get_db),
) -> ScriptKillResponse:
    entry = script_registry.get(execution_id)
    if entry is None or not script_registry.mark_kill_requested(execution_id):
        raise HTTPException(status_code=404, detail="No running execution with that id")

    process = cast(asyncio.subprocess.Process, entry.process)
    process.terminate()  # SIGTERM — graceful
    try:
        await asyncio.wait_for(process.wait(), KILL_GRACE_SECONDS)
    except asyncio.TimeoutError:
        process.kill()  # SIGKILL — last resort
        await process.wait()

    script_registry.unregister(execution_id)
    await _finish(execution_id, ScriptStatus.killed, -15, "", "Terminated by admin")

    row = await db.get(ScriptExecution, execution_id)
    message = f"Script {row.script_name if row else execution_id} terminated (SIGTERM, SIGKILL after {KILL_GRACE_SECONDS}s grace)"
    return ScriptKillResponse(execution_id=execution_id, status=ScriptStatus.killed, message=message)


@router.get("", response_model=list[ScriptStatusOut])
async def list_scripts(
    user: Annotated[User, Depends(require_admin)],
    limit: int = Query(50, le=200),
    db=Depends(get_db),
) -> list[ScriptStatusOut]:
    r = await db.execute(select(ScriptExecution).order_by(ScriptExecution.started_at.desc()).limit(limit))
    return r.scalars().all()


@router.get("/{execution_id}", response_model=ScriptStatusOut)
async def script_status(
    execution_id: uuid.UUID,
    user: Annotated[User, Depends(require_admin)],
    db=Depends(get_db),
) -> ScriptStatusOut:
    row = await db.get(ScriptExecution, execution_id)
    if row is None:
        raise HTTPException(status_code=404, detail="Execution not found")
    return row


@router.get("/{execution_id}/events")
async def script_events(
    execution_id: uuid.UUID,
    user: Annotated[User, Depends(require_admin)],
) -> StreamingResponse:
    if execution_id not in _queues:
        raise HTTPException(status_code=404, detail="No live stream for that execution")

    async def gen():
        queue = _queues[execution_id]
        while True:
            try:
                item = await asyncio.wait_for(queue.get(), timeout=15.0)
            except asyncio.TimeoutError:
                yield ": keepalive\n\n"
                continue
            if item is None:
                break
            yield item

    return StreamingResponse(gen(), media_type="text/event-stream")
