"""User model."""
import uuid
from datetime import datetime

from sqlalchemy import Boolean, DateTime, Enum, String, func
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.core.crypto import EncryptedSecret
from app.core.database import Base

import enum


class UserRole(str, enum.Enum):
    member = "member"
    coordinator = "coordinator"
    admin = "admin"


class User(Base):
    __tablename__ = "users"

    id: Mapped[uuid.UUID] = mapped_column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    name: Mapped[str] = mapped_column(String, nullable=False)
    email: Mapped[str] = mapped_column(String, unique=True, nullable=False, index=True)
    roll_no: Mapped[str | None] = mapped_column(String, unique=True, nullable=True)
    role: Mapped[UserRole] = mapped_column(
        Enum(UserRole, name="user_role"), nullable=False, default=UserRole.member
    )
    password_hash: Mapped[str] = mapped_column(String, nullable=False)
    totp_secret: Mapped[str | None] = mapped_column(EncryptedSecret, nullable=True)
    totp_enrolled_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
    is_active: Mapped[bool] = mapped_column(Boolean, nullable=False, default=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), nullable=False
    )

    # Relationships
    permissions: Mapped[list["Permission"]] = relationship("Permission", foreign_keys="Permission.user_id", back_populates="user")
    sessions: Mapped[list["Session"]] = relationship("Session", back_populates="user")
    retrieval_logs: Mapped[list["RetrievalLog"]] = relationship("RetrievalLog", foreign_keys="RetrievalLog.user_id", back_populates="user")
    coordinated_rooms: Mapped[list["Room"]] = relationship("Room", back_populates="coordinator")
    script_executions: Mapped[list["ScriptExecution"]] = relationship("ScriptExecution", back_populates="user")