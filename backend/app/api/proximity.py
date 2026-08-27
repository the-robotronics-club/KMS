"""Proximity router — POST /proximity/verify."""
from typing import Annotated

from fastapi import APIRouter, Cookie, Depends, HTTPException, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.deps import get_current_session
from app.core.database import get_db
from app.schemas import ProximityVerifyRequest, ProximityVerifyResponse
from app.services.proximity_service import ProximityService

router = APIRouter(prefix="/proximity", tags=["proximity"])


@router.post("/verify", response_model=ProximityVerifyResponse)
async def verify_proximity(
    req: ProximityVerifyRequest,
    session: Annotated[dict, Depends(get_current_session)],
    session_id: str | None = Cookie(default=None),
    db: AsyncSession = Depends(get_db),
) -> ProximityVerifyResponse:
    """
    Validates the proximity code from the enclosure's captive portal redirect.
    Sets a proximity-verified flag on the session (5-minute TTL, FR-7).
    """
    try:
        return await ProximityService().verify_code(req, session_id)
    except ValueError as e:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(e))
