"""Sessions router — start, close."""
from typing import Annotated

from fastapi import APIRouter, Cookie, Depends, HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.deps import get_current_user, require_proximity
from app.core.database import get_db
from app.models.device import Device
from app.models.session import Session
from app.models.user import User
from app.schemas import SessionStartRequest, SessionStartResponse
from app.services.mqtt_service import MQTTService

router = APIRouter(prefix="/sessions", tags=["sessions"])


@router.post("/start", response_model=SessionStartResponse, dependencies=[Depends(require_proximity)])
async def start_session(
    req: SessionStartRequest,
    user: Annotated[User, Depends(get_current_user)],
    db: AsyncSession = Depends(get_db),
) -> SessionStartResponse:
    """Open a door session — requires proximity-verified flag (FR-7)."""
    device_r = await db.execute(select(Device).where(Device.id == req.device_id))
    device = device_r.scalar_one_or_none()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")

    mqtt = MQTTService()
    await mqtt.unlock_door(req.device_id, req.session_id)

    db_session = Session(user_id=user.id, device_id=req.device_id)
    db.add(db_session)
    await db.commit()
    await db.refresh(db_session)

    return SessionStartResponse(db_session_id=db_session.id, opened_at=db_session.opened_at)
