"""FastAPI dependency injection helpers — session resolution, auth guards."""
from __future__ import annotations

import uuid
from typing import Annotated

from fastapi import Cookie, Depends, HTTPException, Request, status
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.core.database import get_db
from app.core.security import check_proximity_flag, get_session
from app.models.user import User, UserRole
from app.models.permission import Permission


async def get_current_session(
    session_id: str | None = Cookie(default=None),
) -> dict:
    """Resolve the current session from the session_id cookie."""
    if not session_id:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Not authenticated")
    session = await get_session(session_id)
    if not session:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Session expired")
    return session


async def get_current_user(
    session: Annotated[dict, Depends(get_current_session)],
    db: Annotated[AsyncSession, Depends(get_db)],
) -> User:
    """Resolve the User object from the current session."""
    from sqlalchemy import select
    user_id = uuid.UUID(session["user_id"])
    r = await db.execute(
        select(User)
        .options(selectinload(User.permissions).selectinload(Permission.room))
        .where(User.id == user_id)
    )
    user = r.scalar_one_or_none()
    if not user or not user.is_active:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="User not found or deactivated")
    return user


async def require_admin(user: Annotated[User, Depends(get_current_user)]) -> User:
    if user.role != UserRole.admin:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Admin access required")
    return user


async def require_coordinator_or_admin(user: Annotated[User, Depends(get_current_user)]) -> User:
    if user.role not in (UserRole.admin, UserRole.coordinator):
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Coordinator or admin access required")
    return user


async def require_proximity(
    session_id: str | None = Cookie(default=None),
) -> None:
    """FR-7 — ensure a fresh proximity-verified flag exists for this session."""
    if not session_id:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail={"error": "not_proximity_verified"},
        )
    device_id = await check_proximity_flag(session_id)
    if not device_id:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail={"error": "not_proximity_verified"},
        )
