"""Auth router — login, TOTP, setup, logout, websocket ticket."""
from typing import Annotated

from fastapi import APIRouter, Cookie, Depends, HTTPException, Response, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.config import get_settings
from app.core.database import get_db
from app.api.deps import get_current_user
from app.models.user import User, UserRole
from app.core.security import get_session, issue_ws_ticket
from app.schemas import (
    LoginRequest,
    SessionResponse,
    TOTPSetupRequest,
    TOTPSetupResponse,
    TOTPVerifyRequest,
)
from app.services.auth_service import AuthService

router = APIRouter(prefix="/auth", tags=["auth"])
settings = get_settings()


@router.get("/me")
async def current_user(user: User = Depends(get_current_user)) -> dict:
    # Admins have unrestricted access, so show the system-wide access label.
    # Other users see the rooms/clubs explicitly assigned to them.
    access_names = (
        ["Master"]
        if user.role == UserRole.admin
        else sorted({permission.room.name for permission in user.permissions if permission.room})
    )
    return {
        "id": str(user.id),
        "name": user.name,
        "roll_no": user.roll_no,
        "role": user.role.value,
        "access_names": access_names,
    }


@router.post("/login")
async def login(req: LoginRequest, response: Response, db: AsyncSession = Depends(get_db)) -> dict:
    """Step 1: credentials → returns temp_token for TOTP challenge."""
    try:
        result = await AuthService(db).login(req)
        temp_token = result.pop("temp_token")
        response.set_cookie(
            "auth_challenge", temp_token, httponly=True, samesite="lax",
            secure=settings.cookie_secure, max_age=300, path="/",
        )
        return result
    except (ValueError, PermissionError) as e:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail=str(e))


@router.post("/totp/verify", response_model=SessionResponse)
async def verify_totp(
    req: TOTPVerifyRequest,
    response: Response,
    auth_challenge: str | None = Cookie(default=None),
    db: AsyncSession = Depends(get_db),
) -> SessionResponse:
    """Step 2: TOTP → issues session_id cookie."""
    try:
        if not auth_challenge:
            raise ValueError("Invalid or expired authentication challenge.")
        result = await AuthService(db).verify_totp(req, auth_challenge)
        # Set HttpOnly session cookie
        response.set_cookie(
            "session_id",
            result.session_id,
            httponly=True,
            samesite="lax",
            secure=settings.cookie_secure,
            path="/",
            # Tied to the Redis session TTL — a cookie that outlives the session
            # only produces confusing 401s after the fact.
            max_age=settings.session_ttl_seconds,
        )
        response.delete_cookie("auth_challenge", path="/")
        return result
    except (ValueError, PermissionError) as e:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail=str(e))


@router.post("/totp/setup", response_model=TOTPSetupResponse)
async def setup_totp(
    req: TOTPSetupRequest,
    auth_challenge: str | None = Cookie(default=None),
    db: AsyncSession = Depends(get_db),
) -> TOTPSetupResponse:
    """
    Enroll TOTP — returns QR URI and secret (shown once).

    Authorised by the temp_token from /auth/login, not by a session cookie:
    this runs before the user has any session, since a session is only issued
    after TOTP verification.
    """
    try:
        if not auth_challenge:
            raise ValueError("Invalid or expired authentication challenge.")
        return await AuthService(db).setup_totp_with_temp_token(auth_challenge)
    except ValueError as e:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail=str(e))
    except PermissionError as e:
        raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail=str(e))


@router.post("/ws/ticket")
async def issue_websocket_ticket(
    session_id: Annotated[str | None, Cookie()] = None,
) -> dict:
    """Mint a short-lived ticket for the /ws/keys handshake.

    Authorised by the same session cookie as any other request, because this
    call goes through the frontend's /api proxy. The WebSocket itself cannot —
    see the note in app/core/security.py.
    """
    if not session_id:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Not authenticated")
    if not await get_session(session_id):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Session expired")
    return {"ticket": await issue_ws_ticket(session_id)}


@router.post("/logout", status_code=status.HTTP_204_NO_CONTENT)
async def logout(
    response: Response,
    session_id: str | None = Cookie(default=None),
    db: AsyncSession = Depends(get_db),
) -> None:
    if session_id:
        await AuthService(db).logout(session_id)
    response.delete_cookie("session_id")
    response.delete_cookie("auth_challenge", path="/")
