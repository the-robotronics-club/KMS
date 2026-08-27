"""Device model."""
import uuid
from datetime import datetime

from sqlalchemy import Boolean, DateTime, SmallInteger, String
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.core.database import Base


class Device(Base):
    __tablename__ = "devices"

    id: Mapped[uuid.UUID] = mapped_column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    name: Mapped[str] = mapped_column(String, nullable=False)
    location: Mapped[str | None] = mapped_column(String, nullable=True)
    firmware_version: Mapped[str | None] = mapped_column(String, nullable=True)
    last_heartbeat_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
    battery_pct: Mapped[int | None] = mapped_column(SmallInteger, nullable=True)
    on_backup_power: Mapped[bool] = mapped_column(Boolean, default=False)
    wifi_rssi: Mapped[int | None] = mapped_column(SmallInteger, nullable=True)
    status: Mapped[str] = mapped_column(String, default="offline")

    key_slots: Mapped[list["KeySlot"]] = relationship("KeySlot", back_populates="device")
    sessions: Mapped[list["Session"]] = relationship("Session", back_populates="device")
