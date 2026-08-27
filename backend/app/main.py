import asyncio
import json

import bcrypt
# Passlib + Bcrypt compatibility patch
if not hasattr(bcrypt, "__about__"):
    class About:
        __version__ = getattr(bcrypt, "__version__", "4.0.0")
    bcrypt.__about__ = About

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware

from app.api import admin, auth, keys, proximity, scripts, sessions
from app.core.config import get_settings
from app.core.redis_client import close_redis, get_redis, live_status_channel
from app.core.security import redeem_ws_ticket
from app.workers.mqtt_listener import run_mqtt_listener
from app.workers.scheduler import start_scheduler, stop_scheduler

settings = get_settings()

app = FastAPI(
    title="SNTC API",
    description="Smart Key Storage System — IIT Mandi SNTC",
    version="1.0.0",
    # The schema endpoints publish every route, field and auth shape. Debug only.
    # openapi_url must be nulled too — Swagger UI is just a reader for it.
    docs_url="/docs" if settings.debug else None,
    redoc_url="/redoc" if settings.debug else None,
    openapi_url="/openapi.json" if settings.debug else None,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.allowed_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Register routers
app.include_router(proximity.router)
app.include_router(auth.router)
app.include_router(sessions.router)
app.include_router(keys.router)
app.include_router(admin.router)
app.include_router(scripts.router)


@app.on_event("startup")
async def startup() -> None:
    # Start APScheduler for notification jobs
    start_scheduler()
    if settings.mqtt_enabled:
        asyncio.create_task(run_mqtt_listener())
    else:
        print("[MQTT] MQTT_ENABLED is false — listener not started.")


@app.on_event("shutdown")
async def shutdown() -> None:
    stop_scheduler()
    await close_redis()


@app.get("/health", tags=["health"])
async def health() -> dict:
    return {"status": "ok", "service": "SNTC API"}


@app.websocket("/ws/keys")
async def websocket_keys(websocket: WebSocket, ticket: str | None = None):
    """WebSocket endpoint for real-time key status updates.

    Authenticated by a single-use ticket from POST /auth/ws/ticket, not by the
    session cookie: this socket is opened directly against the backend host,
    while the cookie belongs to the frontend origin and is HttpOnly.
    """
    if not ticket or not await redeem_ws_ticket(ticket):
        # 1008 = policy violation. Same code for missing, unknown and expired so
        # a caller cannot probe which tickets once existed.
        await websocket.close(code=1008)
        return

    await websocket.accept()
    redis = get_redis()
    pubsub = redis.pubsub()
    await pubsub.subscribe(live_status_channel())

    try:
        # Send initial connection confirmation
        await websocket.send_text(json.dumps({"type": "connected"}))

        # Listen for Redis pub/sub messages and forward to WebSocket
        async for message in pubsub.listen():
            if message["type"] == "message":
                await websocket.send_text(message["data"])
    except WebSocketDisconnect:
        pass
    finally:
        await pubsub.unsubscribe(live_status_channel())
        await pubsub.aclose()