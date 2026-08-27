"""Keys router — list, retrieve, return, extend."""
from typing import Annotated

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.deps import get_current_user, require_proximity
from app.core.database import get_db
from app.models.user import User
from app.schemas import (
    ExtendRequest,
    ExtendResponse,
    KeySlotOut,
    RetrieveRequest,
    RetrieveResponse,
    ReturnRequest,
    ReturnResponse,
)
from app.services.key_service import KeyService
import uuid

router = APIRouter(prefix="/keys", tags=["keys"])


@router.get("", response_model=list[KeySlotOut])
async def list_keys(
    user: Annotated[User, Depends(get_current_user)],
    db: AsyncSession = Depends(get_db),
) -> list[KeySlotOut]:
    """Get all key slots the user has permission for (no proximity required — browsing only)."""
    return await KeyService(db).list_slots(user.id)


@router.post("/{slot_id}/retrieve", response_model=RetrieveResponse, dependencies=[Depends(require_proximity)])
async def retrieve_key(
    slot_id: uuid.UUID,
    req: RetrieveRequest,
    user: Annotated[User, Depends(get_current_user)],
    db: AsyncSession = Depends(get_db),
) -> RetrieveResponse:
    """Retrieve a key — requires proximity-verified flag + permission."""
    try:
        return await KeyService(db).retrieve(slot_id, user.id, req)
    except PermissionError as e:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail=str(e))
    except RuntimeError as e:
        if "slot_unavailable" in str(e):
            raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail={"error": "slot_unavailable"})
        raise HTTPException(status_code=status.HTTP_500_INTERNAL_SERVER_ERROR, detail=str(e))
    except ValueError as e:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=str(e))


@router.post("/{slot_id}/return", response_model=ReturnResponse, dependencies=[Depends(require_proximity)])
async def return_key(
    slot_id: uuid.UUID,
    req: ReturnRequest,
    user: Annotated[User, Depends(get_current_user)],
    db: AsyncSession = Depends(get_db),
) -> ReturnResponse:
    """Return a key — requires proximity-verified flag."""
    try:
        return await KeyService(db).return_key(slot_id, user.id, req)
    except PermissionError as e:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail=str(e))
    except ValueError as e:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=str(e))


@router.post("/{slot_id}/extend", response_model=ExtendResponse)
async def extend_possession(
    slot_id: uuid.UUID,
    req: ExtendRequest,
    user: Annotated[User, Depends(get_current_user)],
    db: AsyncSession = Depends(get_db),
) -> ExtendResponse:
    """Extend due time — no proximity required."""
    try:
        return await KeyService(db).extend(slot_id, user.id, req)
    except PermissionError as e:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail=str(e))
