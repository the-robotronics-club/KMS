"""Admin service — CRUD for users, rooms, permissions, scoped by RBAC."""
from __future__ import annotations

import csv
import io
import uuid
from datetime import datetime, timezone

from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.security import hash_password
from app.models.access_log import AccessLog
from app.models.device import Device
from app.models.key_slot import KeySlot
from app.models.override_log import OverrideLog
from app.models.permission import Permission
from app.models.retrieval_log import RetrievalLog, RetrievalStatus
from app.models.room import Room
from app.models.user import User, UserRole
from app.schemas import (
    DashboardSummary,
    DeviceCreate,
    DeviceOut,
    PermissionCreate,
    PermissionOut,
    RoomCreate,
    RoomOut,
    UserCreate,
    UserOut,
    UserUpdate,
    UsageReportResponse,
    UsageReportRow,
)


class AdminService:
    def __init__(self, db: AsyncSession):
        self.db = db

    # ── Users ────────────────────────────────────────────────────────────────

    async def list_users(self, skip: int = 0, limit: int = 50) -> list[UserOut]:
        r = await self.db.execute(select(User).offset(skip).limit(limit))
        return [UserOut.model_validate(u) for u in r.scalars()]

    async def create_user(self, req: UserCreate, created_by: uuid.UUID) -> UserOut:
        user = User(
            name=req.name,
            email=req.email,
            roll_no=req.roll_no,
            role=req.role,
            password_hash=hash_password(req.password),
        )
        self.db.add(user)
        await self.db.commit()
        await self.db.refresh(user)
        await self._log_admin_action(created_by, "user_created", {"email": req.email})
        return UserOut.model_validate(user)

    async def update_user(self, user_id: uuid.UUID, req: UserUpdate, updated_by: uuid.UUID) -> UserOut:
        r = await self.db.execute(select(User).where(User.id == user_id))
        user = r.scalar_one_or_none()
        if not user:
            raise ValueError("User not found.")
        if req.name is not None:
            user.name = req.name
        if req.role is not None:
            user.role = req.role
        if req.is_active is not None:
            user.is_active = req.is_active
        if req.roll_no is not None:
            user.roll_no = req.roll_no
        self.db.add(user)
        await self.db.commit()
        await self._log_admin_action(updated_by, "user_updated", {"user_id": str(user_id)})
        return UserOut.model_validate(user)

    async def bulk_import_users(self, csv_content: str, created_by: uuid.UUID) -> dict:
        reader = csv.DictReader(io.StringIO(csv_content))
        created, errors = 0, []
        for row in reader:
            try:
                req = UserCreate(
                    name=row["name"],
                    email=row["email"],
                    roll_no=row.get("roll_no"),
                    role=UserRole(row.get("role", "member")),
                    password=row.get("password", row["roll_no"] or "changeme123"),
                )
                await self.create_user(req, created_by)
                created += 1
            except Exception as e:
                errors.append({"row": row, "error": str(e)})
        return {"created": created, "errors": errors}

    # ── Permissions ───────────────────────────────────────────────────────────

    async def grant_permission(
        self, req: PermissionCreate, granted_by: uuid.UUID, granter_role: UserRole, granter_rooms: list[uuid.UUID]
    ) -> PermissionOut:
        # Scope enforcement: coordinator can only grant for their own rooms
        if granter_role == UserRole.coordinator and req.room_id not in granter_rooms:
            raise PermissionError("Coordinators can only grant access to their own rooms.")

        perm = Permission(
            user_id=req.user_id,
            room_id=req.room_id,
            granted_by=granted_by,
            expires_at=req.expires_at,
        )
        self.db.add(perm)
        await self.db.commit()
        await self.db.refresh(perm)
        await self._log_admin_action(granted_by, "permission_granted", {
            "user_id": str(req.user_id), "room_id": str(req.room_id)
        })
        return PermissionOut.model_validate(perm)

    async def revoke_permission(
        self, perm_id: uuid.UUID, revoked_by: uuid.UUID, revoker_role: UserRole, revoker_rooms: list[uuid.UUID]
    ) -> None:
        r = await self.db.execute(select(Permission).where(Permission.id == perm_id))
        perm = r.scalar_one_or_none()
        if not perm:
            raise ValueError("Permission not found.")
        if revoker_role == UserRole.coordinator and perm.room_id not in revoker_rooms:
            raise PermissionError("Coordinators can only revoke access for their own rooms.")
        await self.db.delete(perm)
        await self.db.commit()
        await self._log_admin_action(revoked_by, "permission_revoked", {"perm_id": str(perm_id)})

    # ── Rooms ─────────────────────────────────────────────────────────────────

    async def list_rooms(self) -> list[RoomOut]:
        r = await self.db.execute(select(Room))
        return [RoomOut.model_validate(room) for room in r.scalars()]

    async def create_room(self, req: RoomCreate, created_by: uuid.UUID) -> RoomOut:
        room = Room(**req.model_dump())
        self.db.add(room)
        await self.db.commit()
        await self.db.refresh(room)
        return RoomOut.model_validate(room)

    async def get_coordinator_rooms(self, coordinator_id: uuid.UUID) -> list[uuid.UUID]:
        r = await self.db.execute(select(Room.id).where(Room.coordinator_id == coordinator_id))
        return [row[0] for row in r.fetchall()]

    # ── Devices ───────────────────────────────────────────────────────────────

    async def list_devices(self) -> list[DeviceOut]:
        r = await self.db.execute(select(Device))
        return [DeviceOut.model_validate(d) for d in r.scalars()]

    async def create_device(self, req: DeviceCreate) -> DeviceOut:
        device = Device(**req.model_dump())
        self.db.add(device)
        await self.db.commit()
        await self.db.refresh(device)
        return DeviceOut.model_validate(device)

    async def toggle_maintenance(self, device_id: uuid.UUID, by: uuid.UUID) -> None:
        r = await self.db.execute(select(KeySlot).where(KeySlot.device_id == device_id))
        slots = r.scalars().all()
        from app.models.key_slot import KeyStatus
        for slot in slots:
            if slot.status == KeyStatus.available:
                slot.status = KeyStatus.maintenance
            elif slot.status == KeyStatus.maintenance:
                slot.status = KeyStatus.available
            self.db.add(slot)
        await self.db.commit()
        await self._log_admin_action(by, "device_maintenance_toggled", {"device_id": str(device_id)})

    # ── Override logs ─────────────────────────────────────────────────────────

    async def resolve_override(self, override_id: uuid.UUID, note: str, resolved_by: uuid.UUID) -> None:
        r = await self.db.execute(select(OverrideLog).where(OverrideLog.id == override_id))
        log = r.scalar_one_or_none()
        if not log:
            raise ValueError("Override log not found.")
        log.resolution_note = note
        log.resolved_by = resolved_by
        log.resolved_at = datetime.now(timezone.utc)
        self.db.add(log)
        await self.db.commit()

    # ── Dashboard ─────────────────────────────────────────────────────────────

    async def get_dashboard(self, room_ids: list[uuid.UUID] | None = None) -> DashboardSummary:
        from app.models.key_slot import KeyStatus as KS
        now = datetime.now(timezone.utc)

        slot_ids = select(KeySlot.id)
        device_ids = select(KeySlot.device_id)
        if room_ids is not None:
            slot_ids = slot_ids.where(KeySlot.room_id.in_(room_ids))
            device_ids = device_ids.where(KeySlot.room_id.in_(room_ids))

        key_conditions = [KeySlot.status == KS.retrieved]
        if room_ids is not None:
            key_conditions.append(KeySlot.room_id.in_(room_ids))

        keys_out_r = await self.db.execute(
            select(func.count()).select_from(KeySlot).where(*key_conditions)
        )
        keys_out = keys_out_r.scalar() or 0

        overdue_r = await self.db.execute(
            select(func.count()).select_from(RetrievalLog).where(
                RetrievalLog.status.in_((RetrievalStatus.active, RetrievalStatus.overdue)),
                RetrievalLog.due_at < now,
                RetrievalLog.key_slot_id.in_(slot_ids),
            )
        )
        overdue_count = overdue_r.scalar() or 0

        tamper_r = await self.db.execute(
            select(func.count()).select_from(OverrideLog).where(
                OverrideLog.resolved_at == None,
                *([OverrideLog.device_id.in_(device_ids)] if room_ids is not None else []),
            )
        )
        tamper_count = tamper_r.scalar() or 0

        device_query = select(Device)
        if room_ids is not None:
            device_query = device_query.where(Device.id.in_(device_ids))
        device_result = await self.db.execute(device_query)
        devices = [DeviceOut.model_validate(d) for d in device_result.scalars()]

        from datetime import date
        today_start = datetime.combine(date.today(), datetime.min.time()).replace(tzinfo=timezone.utc)
        today_conditions = [RetrievalLog.retrieved_at >= today_start]
        if room_ids is not None:
            today_conditions.append(RetrievalLog.key_slot_id.in_(slot_ids))
        today_r = await self.db.execute(
            select(func.count()).select_from(RetrievalLog).where(*today_conditions)
        )
        today_count = today_r.scalar() or 0

        return DashboardSummary(
            keys_out=keys_out,
            overdue_count=overdue_count,
            unresolved_tamper_count=tamper_count,
            device_summary=devices,
            today_retrieval_count=today_count,
        )

    # ── Reports ───────────────────────────────────────────────────────────────

    async def get_usage_report(
        self, room_ids: list[uuid.UUID] | None = None
    ) -> UsageReportResponse:
        rooms_r = await self.db.execute(select(Room))
        rooms = rooms_r.scalars().all()
        if room_ids:
            rooms = [r for r in rooms if r.id in room_ids]

        rows = []
        for room in rooms:
            slots_r = await self.db.execute(select(KeySlot).where(KeySlot.room_id == room.id))
            slot_ids = [s.id for s in slots_r.scalars()]
            if not slot_ids:
                continue

            logs_r = await self.db.execute(
                select(RetrievalLog).where(RetrievalLog.key_slot_id.in_(slot_ids))
            )
            logs = logs_r.scalars().all()
            total = len(logs)
            overdue = sum(1 for l in logs if l.status == RetrievalStatus.overdue)
            if total > 0:
                durations = []
                for l in logs:
                    if l.returned_at:
                        durations.append((l.returned_at - l.retrieved_at).total_seconds() / 60)
                avg_min = sum(durations) / len(durations) if durations else 0.0
            else:
                avg_min = 0.0

            rows.append(UsageReportRow(
                room_id=room.id,
                room_name=room.name,
                total_retrievals=total,
                avg_possession_minutes=round(avg_min, 1),
                overdue_count=overdue,
            ))

        return UsageReportResponse(rows=rows, generated_at=datetime.now(timezone.utc))

    # ── Helper ────────────────────────────────────────────────────────────────

    async def _log_admin_action(self, user_id: uuid.UUID, event_type: str, metadata: dict) -> None:
        log = AccessLog(user_id=user_id, event_type=event_type, metadata_=metadata)
        self.db.add(log)
        await self.db.commit()
