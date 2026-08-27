import secrets
import uuid
from datetime import datetime, timezone

import bcrypt
# Passlib + Bcrypt compatibility patch
if not hasattr(bcrypt, "__about__"):
    class About:
        __version__ = getattr(bcrypt, "__version__", "4.0.0")
    bcrypt.__about__ = About

import pyotp
from passlib.context import CryptContext

from app.core.config import get_settings
from app.core.redis_client import (
    get_redis,
    login_attempt_key,
    proximity_flag_key,
    session_key,
    totp_attempt_key,
    ws_ticket_key,
)

settings = get_settings()

# ── Password hashing ─────────────────────────────────────────────────────────
_pwd_context = CryptContext(schemes=["bcrypt"], deprecated="auto")


def hash_password(plain: str) -> str:
    return _pwd_context.hash(plain)


def verify_password(plain: str, hashed: str) -> bool:
    return _pwd_context.verify(plain, hashed)


# ── TOTP ─────────────────────────────────────────────────────────────────────

def generate_totp_secret() -> str:
    """Generate a new base32 TOTP secret."""
    return pyotp.random_base32()


def get_totp_uri(secret: str, user_email: str) -> str:
    """Return an otpauth:// URI for QR code generation."""
    totp = pyotp.TOTP(secret)
    return totp.provisioning_uri(name=user_email, issuer_name="SNTC IIT Mandi")


def verify_totp(secret: str, code: str) -> bool:
    """Verify TOTP code with ±1 step (RFC 6238 §6.2)."""
    # Demo escape hatch: lets a presenter in without an authenticator app.
    # Gated on an env var that is unset everywhere except a local demo box —
    # if this is ever true in production, TOTP is effectively off.
    bypass = get_settings().totp_demo_bypass_code
    if bypass and code == bypass:
        return True
    totp = pyotp.TOTP(secret)
    return totp.verify(code, valid_window=1)


# ── Session tokens ────────────────────────────────────────────────────────────

def generate_session_id() -> str:
    return str(uuid.uuid4())


async def create_session(user_id: str, extra: dict | None = None) -> str:
    """Store session in Redis, return session_id."""
    redis = get_redis()
    sid = generate_session_id()
    data = {"user_id": user_id, "created_at": datetime.now(timezone.utc).isoformat()}
    if extra:
        data.update(extra)
    await redis.hset(session_key(sid), mapping=data)
    await redis.expire(session_key(sid), settings.session_ttl_seconds)
    return sid


async def get_session(session_id: str) -> dict | None:
    """Retrieve session from Redis. Returns None if not found/expired."""
    redis = get_redis()
    data = await redis.hgetall(session_key(session_id))
    return data if data else None


async def delete_session(session_id: str) -> None:
    redis = get_redis()
    await redis.delete(session_key(session_id))


async def consume_temp_token(token: str) -> str | None:
    return await get_redis().getdel(f"temp:{token}")


# ── Proximity flag ────────────────────────────────────────────────────────────

async def set_proximity_flag(session_id: str, device_id: str) -> None:
    redis = get_redis()
    await redis.setex(
        proximity_flag_key(session_id),
        settings.proximity_flag_ttl_seconds,
        device_id,
    )


async def check_proximity_flag(session_id: str) -> str | None:
    """Return device_id if flag is still valid, else None."""
    redis = get_redis()
    return await redis.get(proximity_flag_key(session_id))


# ── Rate limiting ─────────────────────────────────────────────────────────────

async def record_login_attempt(identifier: str) -> int:
    """Increment and return the attempt count. Sets TTL on first attempt."""
    redis = get_redis()
    key = login_attempt_key(identifier)
    count = await redis.incr(key)
    if count == 1:
        await redis.expire(key, settings.login_lockout_seconds)
    return count


async def is_login_locked(identifier: str) -> bool:
    redis = get_redis()
    count = await redis.get(login_attempt_key(identifier))
    return int(count or 0) >= settings.login_max_attempts


async def clear_login_attempts(identifier: str) -> None:
    redis = get_redis()
    await redis.delete(login_attempt_key(identifier))


async def record_totp_attempt(user_id: str) -> int:
    redis = get_redis()
    key = totp_attempt_key(user_id)
    count = await redis.incr(key)
    if count == 1:
        await redis.expire(key, settings.totp_lockout_seconds)
    return count


async def is_totp_locked(user_id: str) -> bool:
    redis = get_redis()
    count = await redis.get(totp_attempt_key(user_id))
    return int(count or 0) >= settings.totp_max_attempts


async def clear_totp_attempts(user_id: str) -> None:
    redis = get_redis()
    await redis.delete(totp_attempt_key(user_id))


# ── Nonce (replay protection) ─────────────────────────────────────────────────

def generate_nonce() -> str:
    return secrets.token_hex(16)


async def consume_nonce(nonce: str, ttl: int = 300) -> bool:
    """Store nonce in Redis; return False if already seen (replay)."""
    redis = get_redis()
    key = f"nonce:{nonce}"
    set_ok = await redis.set(key, "1", ex=ttl, nx=True)
    return bool(set_ok)


# ── WebSocket tickets ─────────────────────────────────────────────────────────
# /ws/keys is opened straight against the backend host, bypassing the frontend's
# /api proxy, so the session cookie — which belongs to the frontend origin and is
# HttpOnly — can never reach it. A ticket carries the session across that gap:
# minted over the authenticated proxy, redeemed once, expires in seconds.

async def issue_ws_ticket(session_id: str) -> str:
    """Mint a single-use ticket that maps to an existing session."""
    redis = get_redis()
    ticket = generate_nonce()
    await redis.setex(ws_ticket_key(ticket), settings.ws_ticket_ttl_seconds, session_id)
    return ticket


async def redeem_ws_ticket(ticket: str) -> str | None:
    """Return the session_id for a ticket and burn it. None if unknown/expired."""
    redis = get_redis()
    # GETDEL is atomic, so two concurrent redemptions cannot both win.
    return await redis.getdel(ws_ticket_key(ticket))
