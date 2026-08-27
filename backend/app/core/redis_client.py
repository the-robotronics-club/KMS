"""Redis async client wrapper (Upstash-compatible)."""
import redis.asyncio as aioredis

from app.core.config import get_settings

settings = get_settings()

_redis_client: aioredis.Redis | None = None


def get_redis() -> aioredis.Redis:
    global _redis_client
    if _redis_client is None:
        _redis_client = aioredis.from_url(
            settings.redis_url,
            decode_responses=True,
            socket_timeout=5,
            socket_connect_timeout=5,
        )
    return _redis_client


async def close_redis() -> None:
    global _redis_client
    if _redis_client:
        await _redis_client.aclose()
        _redis_client = None


# ── Key helpers ─────────────────────────────────────────────────────────────

def session_key(session_id: str) -> str:
    return f"session:{session_id}"


def proximity_code_key(code: str) -> str:
    return f"proximity:code:{code}"


def proximity_flag_key(session_id: str) -> str:
    return f"proximity:flag:{session_id}"


def login_attempt_key(identifier: str) -> str:
    return f"rate:login:{identifier}"


def totp_attempt_key(user_id: str) -> str:
    return f"rate:totp:{user_id}"


def ws_ticket_key(ticket: str) -> str:
    return f"ws:ticket:{ticket}"


def live_status_channel() -> str:
    return "skss:key_status"
