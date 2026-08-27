"""Pydantic schemas for Auth, Proximity, Keys, Admin, Devices, Notifications, Reports."""
from __future__ import annotations

import uuid
from datetime import datetime
from typing import Any

from pydantic import BaseModel, EmailStr, Field, field_validator

from app.models.key_slot import KeyStatus
from app.models.override_log import OverrideTrigger
from app.models.retrieval_log import RetrievalStatus
from app.models.script_execution import ScriptStatus
from app.models.user import UserRole


# ── Auth ──────────────────────────────────────────────────────────────────────

class LoginRequest(BaseModel):
    email: EmailStr
    password: str


class TOTPVerifyRequest(BaseModel):
    code: str


class TOTPSetupRequest(BaseModel):
    pass


class TOTPSetupResponse(BaseModel):
    totp_uri: str
    secret: str   # shown once, for manual entry


class SessionResponse(BaseModel):
    session_id: str | None = Field(default=None, exclude=True)
    user_id: uuid.UUID
    role: UserRole


class LogoutRequest(BaseModel):
    session_id: str


# ── Proximity ─────────────────────────────────────────────────────────────────

class ProximityVerifyRequest(BaseModel):
    code: str
    # Optional: the code alone already resolves to a device in Redis. Only sent
    # when a captive-portal redirect supplied it in the URL.
    device_id: uuid.UUID | None = None


class ProximityVerifyResponse(BaseModel):
    proximity_verified: bool
    expires_in_seconds: int


# ── Sessions ──────────────────────────────────────────────────────────────────

class SessionStartRequest(BaseModel):
    session_id: str
    device_id: uuid.UUID


class SessionStartResponse(BaseModel):
    db_session_id: uuid.UUID
    opened_at: datetime


# ── Keys ──────────────────────────────────────────────────────────────────────

class KeySlotOut(BaseModel):
    slot_id: uuid.UUID
    slot_number: int
    room_id: uuid.UUID | None
    room_name: str | None
    status: KeyStatus
    current_holder: str | None   # user name if retrieved
    due_at: datetime | None

    model_config = {"from_attributes": True}


class RetrieveRequest(BaseModel):
    # FK to the door session opened by POST /sessions/start, when there is one.
    # Typed as a UUID so a malformed value is rejected at the boundary with a 422
    # instead of raising mid-transaction, after the slot has already been flipped.
    session_id: uuid.UUID | None = None


class RetrieveResponse(BaseModel):
    slot_id: uuid.UUID
    status: KeyStatus
    due_at: datetime
    retrieval_log_id: uuid.UUID


class ReturnRequest(BaseModel):
    session_id: uuid.UUID | None = None


class ReturnResponse(BaseModel):
    slot_id: uuid.UUID
    status: KeyStatus
    returned_at: datetime


class ExtendRequest(BaseModel):
    session_id: uuid.UUID | None = None
    additional_hours: int = 6

    @field_validator("additional_hours")
    @classmethod
    def validate_hours(cls, v: int) -> int:
        if not 1 <= v <= 12:
            raise ValueError("additional_hours must be 1–12")
        return v


class ExtendResponse(BaseModel):
    slot_id: uuid.UUID
    new_due_at: datetime
    extension_count: int


# ── Scripts ───────────────────────────────────────────────────────────────────

class ScriptRunRequest(BaseModel):
    script_name: str


class ScriptRunResponse(BaseModel):
    execution_id: uuid.UUID
    status: ScriptStatus


class ScriptKillResponse(BaseModel):
    execution_id: uuid.UUID
    status: ScriptStatus
    message: str


class ScriptStatusOut(BaseModel):
    id: uuid.UUID
    user_id: uuid.UUID
    script_name: str
    status: ScriptStatus
    pid: int | None
    started_at: datetime
    finished_at: datetime | None
    exit_code: int | None
    output: str | None
    error: str | None

    model_config = {"from_attributes": True}


# ── Admin — Users ─────────────────────────────────────────────────────────────

class UserCreate(BaseModel):
    name: str
    email: EmailStr
    roll_no: str | None = None
    role: UserRole = UserRole.member
    password: str


class UserUpdate(BaseModel):
    name: str | None = None
    role: UserRole | None = None
    is_active: bool | None = None
    roll_no: str | None = None


class UserOut(BaseModel):
    id: uuid.UUID
    name: str
    email: str
    roll_no: str | None
    role: UserRole
    is_active: bool
    totp_enrolled_at: datetime | None
    created_at: datetime

    model_config = {"from_attributes": True}


# ── Admin — Permissions ───────────────────────────────────────────────────────

class PermissionCreate(BaseModel):
    user_id: uuid.UUID
    room_id: uuid.UUID
    expires_at: datetime | None = None


class PermissionOut(BaseModel):
    id: uuid.UUID
    user_id: uuid.UUID
    room_id: uuid.UUID
    granted_by: uuid.UUID | None
    granted_at: datetime
    expires_at: datetime | None

    model_config = {"from_attributes": True}


# ── Admin — Rooms ─────────────────────────────────────────────────────────────

class RoomCreate(BaseModel):
    name: str
    block: str | None = None
    description: str | None = None
    coordinator_id: uuid.UUID | None = None


class RoomOut(BaseModel):
    id: uuid.UUID
    name: str
    block: str | None
    description: str | None
    coordinator_id: uuid.UUID | None

    model_config = {"from_attributes": True}


# ── Admin — Logs ──────────────────────────────────────────────────────────────

class AccessLogOut(BaseModel):
    id: uuid.UUID
    user_id: uuid.UUID | None
    device_id: uuid.UUID | None
    event_type: str
    ts: datetime
    metadata_: dict | None

    model_config = {"from_attributes": True}


class RetrievalLogOut(BaseModel):
    id: uuid.UUID
    key_slot_id: uuid.UUID
    user_id: uuid.UUID
    session_id: uuid.UUID | None
    retrieved_at: datetime
    due_at: datetime
    returned_at: datetime | None
    extension_count: int
    reminder_count: int
    status: RetrievalStatus

    model_config = {"from_attributes": True}


class OverrideLogOut(BaseModel):
    id: uuid.UUID
    device_id: uuid.UUID
    triggered_by: OverrideTrigger
    reason: str | None
    ts: datetime
    resolved_by: uuid.UUID | None
    resolved_at: datetime | None
    resolution_note: str | None

    model_config = {"from_attributes": True}


class OverrideResolveRequest(BaseModel):
    resolution_note: str


# ── Admin — Devices ───────────────────────────────────────────────────────────

class DeviceOut(BaseModel):
    id: uuid.UUID
    name: str
    location: str | None
    firmware_version: str | None
    last_heartbeat_at: datetime | None
    battery_pct: int | None
    on_backup_power: bool
    wifi_rssi: int | None
    status: str

    model_config = {"from_attributes": True}


class DeviceCreate(BaseModel):
    name: str
    location: str | None = None


# ── Admin — Reports ───────────────────────────────────────────────────────────

class UsageReportRow(BaseModel):
    room_id: uuid.UUID
    room_name: str
    total_retrievals: int
    avg_possession_minutes: float
    overdue_count: int


class UsageReportResponse(BaseModel):
    rows: list[UsageReportRow]
    generated_at: datetime


# ── Dashboard ─────────────────────────────────────────────────────────────────

class DashboardSummary(BaseModel):
    keys_out: int
    overdue_count: int
    unresolved_tamper_count: int
    device_summary: list[DeviceOut]
    today_retrieval_count: int


# ── Generic ───────────────────────────────────────────────────────────────────

class ErrorResponse(BaseModel):
    error: str
    detail: Any = None


class PaginatedResponse(BaseModel):
    items: list[Any]
    total: int
    page: int
    page_size: int


class SuccessResponse(BaseModel):
    ok: bool = True
    message: str = "success"
