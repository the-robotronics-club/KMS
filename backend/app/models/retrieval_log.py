"""RetrievalLog model."""
import enum
import uuid
from datetime import datetime

from sqlalchemy import DateTime, Enum, ForeignKey, SmallInteger, func
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.core.database import Base


class RetrievalStatus(str, enum.Enum):
    active = "active"
    returned = "returned"
    overdue = "overdue"


class RetrievalLog(Base):
    __tablename__ = "retrieval_logs"

    id: Mapped[uuid.UUID] = mapped_column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    key_slot_id: Mapped[uuid.UUID] = mapped_column(UUID(as_uuid=True), ForeignKey("key_slots.id"), nullable=False)
    user_id: Mapped[uuid.UUID] = mapped_column(UUID(as_uuid=True), ForeignKey("users.id"), nullable=False)
    session_id: Mapped[uuid.UUID | None] = mapped_column(UUID(as_uuid=True), ForeignKey("sessions.id"), nullable=True)
    retrieved_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now(), nullable=False)
    due_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    returned_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
    extension_count: Mapped[int] = mapped_column(SmallInteger, nullable=False, default=0)
    reminder_count: Mapped[int] = mapped_column(SmallInteger, nullable=False, default=0)
    status: Mapped[RetrievalStatus] = mapped_column(
        Enum(RetrievalStatus, name="retrieval_status"), nullable=False, default=RetrievalStatus.active
    )

    key_slot: Mapped["KeySlot"] = relationship("KeySlot", back_populates="retrieval_logs")
    user: Mapped["User"] = relationship("User", foreign_keys=[user_id], back_populates="retrieval_logs")
    session: Mapped["Session | None"] = relationship("Session", back_populates="retrieval_logs")
