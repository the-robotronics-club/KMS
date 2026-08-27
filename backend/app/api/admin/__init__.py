"""Admin routers — users, permissions, logs, devices, reports, dashboard."""
from typing import Annotated
import uuid

from fastapi import APIRouter, Depends, HTTPException, Query, UploadFile, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.deps import get_current_user, require_admin, require_coordinator_or_admin
from app.core.database import get_db
from app.models.access_log import AccessLog
from app.models.key_slot import KeySlot
from app.models.override_log import OverrideLog
from app.models.retrieval_log import RetrievalLog
from app.models.user import User, UserRole
from app.schemas import (
    AccessLogOut,
    DashboardSummary,
    DeviceCreate,
    DeviceOut,
    OverrideLogOut,
    OverrideResolveRequest,
    PermissionCreate,
    PermissionOut,
    RetrievalLogOut,
    RoomCreate,
    RoomOut,
    SuccessResponse,
    UsageReportResponse,
    UserCreate,
    UserOut,
    UserUpdate,
)
from app.services.admin_service import AdminService

router = APIRouter(prefix="/admin", tags=["admin"])


# ── Dashboard ─────────────────────────────────────────────────────────────────

@router.get("/dashboard", response_model=DashboardSummary)
async def dashboard(
    user: Annotated[User, Depends(require_coordinator_or_admin)],
    db: AsyncSession = Depends(get_db),
) -> DashboardSummary:
    svc = AdminService(db)
    room_ids = await svc.get_coordinator_rooms(user.id) if user.role == UserRole.coordinator else None
    return await svc.get_dashboard(room_ids)


# ── Users ─────────────────────────────────────────────────────────────────────

@router.get("/users", response_model=list[UserOut], dependencies=[Depends(require_admin)])
async def list_users(
    skip: int = Query(0, ge=0), limit: int = Query(50, le=200), db: AsyncSession = Depends(get_db)
) -> list[UserOut]:
    return await AdminService(db).list_users(skip, limit)


@router.post("/users", response_model=UserOut, dependencies=[Depends(require_admin)])
async def create_user(
    req: UserCreate,
    user: Annotated[User, Depends(require_admin)],
    db: AsyncSession = Depends(get_db),
) -> UserOut:
    try:
        return await AdminService(db).create_user(req, user.id)
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.patch("/users/{user_id}", response_model=UserOut, dependencies=[Depends(require_admin)])
async def update_user(
    user_id: uuid.UUID,
    req: UserUpdate,
    user: Annotated[User, Depends(require_admin)],
    db: AsyncSession = Depends(get_db),
) -> UserOut:
    try:
        return await AdminService(db).update_user(user_id, req, user.id)
    except ValueError as e:
        raise HTTPException(status_code=404, detail=str(e))


@router.post("/users/bulk-import", dependencies=[Depends(require_admin)])
async def bulk_import(
    file: UploadFile,
    user: Annotated[User, Depends(require_admin)],
    db: AsyncSession = Depends(get_db),
) -> dict:
    content = (await file.read()).decode("utf-8")
    return await AdminService(db).bulk_import_users(content, user.id)


@router.post("/users/{user_id}/totp/reenroll", response_model=dict, dependencies=[Depends(require_admin)])
async def reenroll_totp(
    user_id: uuid.UUID,
    db: AsyncSession = Depends(get_db),
) -> dict:
    """Admin-initiated TOTP re-enrollment (for lost phone scenario)."""
    from app.services.auth_service import AuthService
    result = await AuthService(db).force_reenroll_totp(user_id)
    return {"totp_uri": result.totp_uri, "secret": result.secret}


# ── Permissions ───────────────────────────────────────────────────────────────

@router.post("/permissions", response_model=PermissionOut)
async def grant_permission(
    req: PermissionCreate,
    user: Annotated[User, Depends(require_coordinator_or_admin)],
    db: AsyncSession = Depends(get_db),
) -> PermissionOut:
    svc = AdminService(db)
    coord_rooms = await svc.get_coordinator_rooms(user.id) if user.role == UserRole.coordinator else []
    try:
        return await svc.grant_permission(req, user.id, user.role, coord_rooms)
    except PermissionError as e:
        raise HTTPException(status_code=403, detail=str(e))


@router.delete("/permissions/{perm_id}", status_code=204)
async def revoke_permission(
    perm_id: uuid.UUID,
    user: Annotated[User, Depends(require_coordinator_or_admin)],
    db: AsyncSession = Depends(get_db),
) -> None:
    svc = AdminService(db)
    coord_rooms = await svc.get_coordinator_rooms(user.id) if user.role == UserRole.coordinator else []
    try:
        await svc.revoke_permission(perm_id, user.id, user.role, coord_rooms)
    except (ValueError, PermissionError) as e:
        raise HTTPException(status_code=400, detail=str(e))


# ── Rooms ─────────────────────────────────────────────────────────────────────

@router.get("/rooms", response_model=list[RoomOut])
async def list_rooms(
    user: Annotated[User, Depends(require_coordinator_or_admin)],
    db: AsyncSession = Depends(get_db),
) -> list[RoomOut]:
    return await AdminService(db).list_rooms()


@router.post("/rooms", response_model=RoomOut, dependencies=[Depends(require_admin)])
async def create_room(
    req: RoomCreate,
    user: Annotated[User, Depends(require_admin)],
    db: AsyncSession = Depends(get_db),
) -> RoomOut:
    return await AdminService(db).create_room(req, user.id)


# ── Logs ──────────────────────────────────────────────────────────────────────

@router.get("/logs/access", response_model=list[AccessLogOut])
async def get_access_logs(
    user: Annotated[User, Depends(require_coordinator_or_admin)],
    skip: int = 0, limit: int = 100,
    db: AsyncSession = Depends(get_db),
) -> list[AccessLogOut]:
    query = select(AccessLog)
    if user.role == UserRole.coordinator:
        room_ids = await AdminService(db).get_coordinator_rooms(user.id)
        device_ids = select(KeySlot.device_id).where(KeySlot.room_id.in_(room_ids))
        query = query.where(AccessLog.device_id.in_(device_ids))
    r = await db.execute(query.order_by(AccessLog.ts.desc()).offset(skip).limit(limit))
    return [AccessLogOut.model_validate(l) for l in r.scalars()]


@router.get("/logs/retrieval", response_model=list[RetrievalLogOut])
async def get_retrieval_logs(
    user: Annotated[User, Depends(require_coordinator_or_admin)],
    skip: int = 0, limit: int = 100,
    db: AsyncSession = Depends(get_db),
) -> list[RetrievalLogOut]:
    query = select(RetrievalLog)
    if user.role == UserRole.coordinator:
        room_ids = await AdminService(db).get_coordinator_rooms(user.id)
        slot_ids = select(KeySlot.id).where(KeySlot.room_id.in_(room_ids))
        query = query.where(RetrievalLog.key_slot_id.in_(slot_ids))
    r = await db.execute(query.order_by(RetrievalLog.retrieved_at.desc()).offset(skip).limit(limit))
    return [RetrievalLogOut.model_validate(l) for l in r.scalars()]


@router.get("/logs/override", response_model=list[OverrideLogOut])
async def get_override_logs(
    user: Annotated[User, Depends(require_coordinator_or_admin)],
    skip: int = 0, limit: int = 100,
    db: AsyncSession = Depends(get_db),
) -> list[OverrideLogOut]:
    query = select(OverrideLog)
    if user.role == UserRole.coordinator:
        room_ids = await AdminService(db).get_coordinator_rooms(user.id)
        device_ids = select(KeySlot.device_id).where(KeySlot.room_id.in_(room_ids))
        query = query.where(OverrideLog.device_id.in_(device_ids))
    r = await db.execute(query.order_by(OverrideLog.ts.desc()).offset(skip).limit(limit))
    return [OverrideLogOut.model_validate(l) for l in r.scalars()]


@router.post("/logs/override/{override_id}/resolve", response_model=SuccessResponse, dependencies=[Depends(require_admin)])
async def resolve_override(
    override_id: uuid.UUID,
    req: OverrideResolveRequest,
    user: Annotated[User, Depends(require_admin)],
    db: AsyncSession = Depends(get_db),
) -> SuccessResponse:
    try:
        await AdminService(db).resolve_override(override_id, req.resolution_note, user.id)
    except ValueError as e:
        raise HTTPException(status_code=404, detail=str(e))
    return SuccessResponse(message="Override resolved.")


# ── Devices ───────────────────────────────────────────────────────────────────

@router.get("/devices", response_model=list[DeviceOut], dependencies=[Depends(require_admin)])
async def list_devices(db: AsyncSession = Depends(get_db)) -> list[DeviceOut]:
    return await AdminService(db).list_devices()


@router.post("/devices", response_model=DeviceOut, dependencies=[Depends(require_admin)])
async def create_device(req: DeviceCreate, db: AsyncSession = Depends(get_db)) -> DeviceOut:
    return await AdminService(db).create_device(req)


@router.post("/devices/{device_id}/maintenance", response_model=SuccessResponse, dependencies=[Depends(require_admin)])
async def toggle_maintenance(
    device_id: uuid.UUID,
    user: Annotated[User, Depends(require_admin)],
    db: AsyncSession = Depends(get_db),
) -> SuccessResponse:
    await AdminService(db).toggle_maintenance(device_id, user.id)
    return SuccessResponse(message="Maintenance mode toggled.")


# ── Reports ───────────────────────────────────────────────────────────────────

@router.get("/reports/usage", response_model=UsageReportResponse)
async def usage_report(
    user: Annotated[User, Depends(require_coordinator_or_admin)],
    db: AsyncSession = Depends(get_db),
) -> UsageReportResponse:
    svc = AdminService(db)
    if user.role == UserRole.coordinator:
        coord_rooms = await svc.get_coordinator_rooms(user.id)
        return await svc.get_usage_report(coord_rooms)
    return await svc.get_usage_report()
