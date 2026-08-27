"""Authentication service — login, TOTP verify/setup, session management."""
from __future__ import annotations

import uuid
import asyncio
from datetime import datetime, timezone

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core import security
from app.core.config import get_settings
from app.core.redis_client import get_redis
from app.models.access_log import AccessLog
from app.models.user import User, UserRole
from app.schemas import LoginRequest, SessionResponse, TOTPSetupResponse, TOTPVerifyRequest

settings = get_settings()


class AuthService:
    def __init__(self, db: AsyncSession):
        self.db = db

    # ── Login (step 1: credentials) ──────────────────────────────────────────

    async def login(self, req: LoginRequest) -> dict:
        """Validates credentials. Returns a temp_token for TOTP challenge."""
        # Rate limit check
        if await security.is_login_locked(req.email):
            await self._log_event(None, None, "login_fail", {"reason": "rate_limited", "email": req.email})
            raise PermissionError("Too many login attempts. Try again in 15 minutes.")

        email = str(req.email).strip().lower()
        user = await self._get_user_by_email(email)
        if not user or not await asyncio.to_thread(security.verify_password, req.password, user.password_hash):
            await security.record_login_attempt(email)
            await self._log_event(
                user.id if user else None, None, "login_fail", {"email": email}
            )
            raise ValueError("Invalid email or password.")

        if not user.is_active:
            raise PermissionError("Account deactivated.")

        await security.clear_login_attempts(req.email)

        if not user.totp_secret:
            # TOTP not enrolled yet — return flag so frontend routes to setup
            temp_token = await self._make_temp_token(str(user.id))
            return {"requires_totp_setup": True, "temp_token": temp_token, "user_id": str(user.id)}

        # Issue a short-lived partial token for TOTP step
        temp_token = await self._make_temp_token(str(user.id))
        await self._log_event(user.id, None, "login_success", {})
        return {"requires_totp": True, "temp_token": temp_token}

    # ── TOTP verify (step 2) ──────────────────────────────────────────────────

    async def verify_totp(self, req: TOTPVerifyRequest, temp_token: str) -> SessionResponse:
        user_id = await security.consume_temp_token(temp_token)
        if not user_id:
            raise ValueError("Invalid or expired token.")

        if await security.is_totp_locked(user_id):
            raise PermissionError("Too many TOTP attempts. Try again in 5 minutes.")

        user = await self._get_user_by_id(uuid.UUID(user_id))
        if not user or not user.totp_secret:
            raise ValueError("TOTP not configured.")

        if not security.verify_totp(user.totp_secret, req.code):
            count = await security.record_totp_attempt(user_id)
            await self._log_event(user.id, None, "totp_fail", {"attempt": count})
            raise ValueError("Invalid TOTP code.")

        await security.clear_totp_attempts(user_id)
        session_id = await security.create_session(user_id, {"role": user.role.value})
        await self._log_event(user.id, None, "totp_success", {})

        return SessionResponse(session_id=session_id, user_id=user.id, role=user.role)

    # ── TOTP setup ────────────────────────────────────────────────────────────

    async def setup_totp(self, user_id: uuid.UUID) -> TOTPSetupResponse:
        user = await self._get_user_by_id(user_id)
        if not user:
            raise ValueError("User not found.")
        secret = security.generate_totp_secret()
        uri = security.get_totp_uri(secret, user.email)
        # The column type encrypts on write (app.core.crypto.EncryptedSecret).
        user.totp_secret = secret
        user.totp_enrolled_at = datetime.now(timezone.utc)
        self.db.add(user)
        await self.db.commit()
        return TOTPSetupResponse(totp_uri=uri, secret=secret)

    async def setup_totp_with_temp_token(self, temp_token: str) -> TOTPSetupResponse:
        """
        First-time enrollment, before any session exists.

        Authorised by the temp_token from login (which required a correct
        password). Enrollment is refused once a secret exists, so a leaked
        token cannot re-enroll an active account — admins use
        force_reenroll_totp for legitimate resets.
        """
        user_id = await self._peek_temp_token(temp_token)
        if not user_id:
            raise ValueError("Invalid or expired token.")
        user = await self._get_user_by_id(uuid.UUID(user_id))
        if not user or not user.is_active:
            raise ValueError("User not found or deactivated.")
        if user.totp_secret:
            raise PermissionError("TOTP already enrolled. Ask an admin to re-enroll you.")
        return await self.setup_totp(user.id)

    async def force_reenroll_totp(self, user_id: uuid.UUID) -> TOTPSetupResponse:
        """Admin-initiated TOTP re-enrollment."""
        user = await self._get_user_by_id(user_id)
        if not user:
            raise ValueError("User not found.")
        user.totp_secret = None
        user.totp_enrolled_at = None
        self.db.add(user)
        await self.db.commit()
        return await self.setup_totp(user_id)

    # ── Logout ────────────────────────────────────────────────────────────────

    async def logout(self, session_id: str) -> None:
        await security.delete_session(session_id)

    # ── Helpers ───────────────────────────────────────────────────────────────

    async def _get_user_by_email(self, email: str) -> User | None:
        result = await self.db.execute(select(User).where(User.email == email))
        return result.scalar_one_or_none()

    async def _get_user_by_id(self, user_id: uuid.UUID) -> User | None:
        result = await self.db.execute(select(User).where(User.id == user_id))
        return result.scalar_one_or_none()

    async def _make_temp_token(self, user_id: str) -> str:
        redis = get_redis()
        token = security.generate_session_id()
        await redis.setex(f"temp:{token}", 300, user_id)  # 5 min
        return token

    async def _peek_temp_token(self, token: str) -> str | None:
        """Read without consuming — the TOTP verify step still needs this token."""
        redis = get_redis()
        return await redis.get(f"temp:{token}")

    async def _log_event(
        self, user_id: uuid.UUID | None, device_id: uuid.UUID | None, event_type: str, metadata: dict
    ) -> None:
        log = AccessLog(
            user_id=user_id,
            device_id=device_id,
            event_type=event_type,
            metadata_=metadata,
        )
        self.db.add(log)
        await self.db.commit()
