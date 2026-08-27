"""Key service — retrieve, return, extend with atomic DB update + MQTT dispatch."""
from __future__ import annotations

import logging
import uuid
from datetime import datetime, timedelta, timezone

from sqlalchemy import select, update
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.config import get_settings
from app.core.redis_client import get_redis, live_status_channel
from app.models.access_log import AccessLog
from app.models.key_slot import KeySlot, KeyStatus
from app.models.permission import Permission
from app.models.retrieval_log import RetrievalLog, RetrievalStatus
from app.models.room import Room
from app.schemas import (
    ExtendRequest,
    ExtendResponse,
    KeySlotOut,
    RetrieveRequest,
    RetrieveResponse,
    ReturnRequest,
    ReturnResponse,
)
from app.services.mqtt_service import MQTTService
from app.services.notification_service import NotificationService

settings = get_settings()
logger = logging.getLogger(__name__)


class KeyService:
    def __init__(self, db: AsyncSession):
        self.db = db
        self.mqtt = MQTTService()
        self.notifier = NotificationService(db)

    # ── List slots ────────────────────────────────────────────────────────────

    async def list_slots(self, user_id: uuid.UUID) -> list[KeySlotOut]:
        """Return all slots the user has permission for, with live status."""
        permitted_rooms = await self._get_permitted_rooms(user_id)
        result = await self.db.execute(
            select(KeySlot).where(KeySlot.room_id.in_(permitted_rooms))
        )
        slots = result.scalars().all()

        out = []
        for slot in slots:
            active_log = await self._get_current_retrieval(slot.id)
            holder_name = None
            due_at = None
            if active_log:
                user_result = await self.db.execute(
                    select(Permission).where(Permission.user_id == active_log.user_id)
                )
                # Get holder name
                from app.models.user import User
                ur = await self.db.execute(select(User).where(User.id == active_log.user_id))
                holder = ur.scalar_one_or_none()
                holder_name = holder.name if holder else "Unknown"
                due_at = active_log.due_at

            room_result = await self.db.execute(select(Room).where(Room.id == slot.room_id))
            room = room_result.scalar_one_or_none()

            out.append(KeySlotOut(
                slot_id=slot.id,
                slot_number=slot.slot_number,
                room_id=slot.room_id,
                room_name=room.name if room else None,
                status=slot.status,
                current_holder=holder_name,
                due_at=due_at,
            ))
        return out

    # ── Retrieve ──────────────────────────────────────────────────────────────

    async def retrieve(
        self, slot_id: uuid.UUID, user_id: uuid.UUID, req: RetrieveRequest
    ) -> RetrieveResponse:
        slot = await self._get_slot(slot_id)
        if not slot:
            raise ValueError("Slot not found.")

        # Permission check
        if not await self._has_permission(user_id, slot.room_id):
            await self._log(user_id, slot.device_id, "retrieve_denied", {"slot_id": str(slot_id)})
            raise PermissionError("Not authorized for this room.")

        # Atomic conditional update (FR-5 — prevents race)
        result = await self.db.execute(
            update(KeySlot)
            .where(KeySlot.id == slot_id, KeySlot.status == KeyStatus.available)
            .values(status=KeyStatus.retrieved)
            .returning(KeySlot.id)
        )
        updated = result.fetchone()
        if not updated:
            raise RuntimeError("slot_unavailable")

        due_at = datetime.now(timezone.utc) + timedelta(hours=settings.default_possession_hours)
        log = RetrievalLog(
            key_slot_id=slot_id,
            user_id=user_id,
            session_id=req.session_id,
            due_at=due_at,
        )
        self.db.add(log)

        # MQTT dispense command
        try:
            nonce = await self.mqtt.dispense_slot(slot.device_id, slot.slot_number)
        except Exception:
            await self.db.rollback()
            raise

        await self.db.commit()
        await self.db.refresh(log)

        # Publish live status update to Redis channel
        redis = get_redis()
        await redis.publish(live_status_channel(), f"retrieved:{slot_id}")

        # Send retrieval confirmation email (FR-8). Best-effort: the key is out
        # and logged, so a mail failure must not fail the request.
        from app.models.user import User
        ur = await self.db.execute(select(User).where(User.id == user_id))
        user = ur.scalar_one_or_none()
        room_result = await self.db.execute(select(Room).where(Room.id == slot.room_id))
        room = room_result.scalar_one_or_none()
        if user and room:
            try:
                await self.notifier.send_retrieval_confirmation(user, room, due_at)
            except Exception:
                logger.exception("Retrieval confirmation email failed for slot %s", slot_id)

        await self._log(user_id, slot.device_id, "key_retrieved", {"slot_id": str(slot_id), "nonce": nonce})

        return RetrieveResponse(slot_id=slot_id, status=KeyStatus.retrieved, due_at=due_at, retrieval_log_id=log.id)

    # ── Return ────────────────────────────────────────────────────────────────

    async def return_key(
        self, slot_id: uuid.UUID, user_id: uuid.UUID, req: ReturnRequest
    ) -> ReturnResponse:
        slot = await self._get_slot(slot_id)
        if not slot:
            raise ValueError("Slot not found.")

        active_log = await self._get_current_retrieval(slot_id)
        if not active_log or active_log.user_id != user_id:
            raise PermissionError("You do not hold this key.")

        # Unlock slot for reinsertion before committing the matching DB state.
        try:
            await self.mqtt.unlock_slot(slot.device_id, slot.slot_number)
        except Exception:
            await self.db.rollback()
            raise

        # Update DB
        now = datetime.now(timezone.utc)
        await self.db.execute(
            update(KeySlot).where(KeySlot.id == slot_id).values(status=KeyStatus.available)
        )
        active_log.returned_at = now
        active_log.status = RetrievalStatus.returned
        self.db.add(active_log)
        await self.db.commit()

        redis = get_redis()
        await redis.publish(live_status_channel(), f"available:{slot_id}")
        await self._log(user_id, slot.device_id, "key_returned", {"slot_id": str(slot_id)})

        return ReturnResponse(slot_id=slot_id, status=KeyStatus.available, returned_at=now)

    # ── Extend ────────────────────────────────────────────────────────────────

    async def extend(
        self, slot_id: uuid.UUID, user_id: uuid.UUID, req: ExtendRequest
    ) -> ExtendResponse:
        active_log = await self._get_current_retrieval(slot_id)
        if not active_log or active_log.user_id != user_id:
            raise PermissionError("You do not hold this key.")

        new_due = active_log.due_at + timedelta(hours=req.additional_hours)
        active_log.due_at = new_due
        active_log.extension_count += 1
        self.db.add(active_log)
        await self.db.commit()

        slot = await self._get_slot(slot_id)
        return ExtendResponse(slot_id=slot_id, new_due_at=new_due, extension_count=active_log.extension_count)

    # ── Helpers ───────────────────────────────────────────────────────────────

    async def _get_slot(self, slot_id: uuid.UUID) -> KeySlot | None:
        r = await self.db.execute(select(KeySlot).where(KeySlot.id == slot_id))
        return r.scalar_one_or_none()

    async def _has_permission(self, user_id: uuid.UUID, room_id: uuid.UUID | None) -> bool:
        if not room_id:
            return False
        now = datetime.now(timezone.utc)
        r = await self.db.execute(
            select(Permission).where(
                Permission.user_id == user_id,
                Permission.room_id == room_id,
                (Permission.expires_at == None) | (Permission.expires_at > now),
            )
        )
        return r.scalar_one_or_none() is not None

    async def _get_permitted_rooms(self, user_id: uuid.UUID) -> list[uuid.UUID]:
        now = datetime.now(timezone.utc)
        r = await self.db.execute(
            select(Permission.room_id).where(
                Permission.user_id == user_id,
                (Permission.expires_at == None) | (Permission.expires_at > now),
            )
        )
        return [row[0] for row in r.fetchall()]

    async def _get_current_retrieval(self, slot_id: uuid.UUID) -> RetrievalLog | None:
        r = await self.db.execute(
            select(RetrievalLog).where(
                RetrievalLog.key_slot_id == slot_id,
                RetrievalLog.status.in_((RetrievalStatus.active, RetrievalStatus.overdue)),
            )
        )
        return r.scalar_one_or_none()

    async def _log(self, user_id: uuid.UUID, device_id: uuid.UUID, event_type: str, metadata: dict) -> None:
        log = AccessLog(user_id=user_id, device_id=device_id, event_type=event_type, metadata_=metadata)
        self.db.add(log)
        await self.db.commit()
