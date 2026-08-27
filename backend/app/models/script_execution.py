"""ScriptExecution model for tracking script runs."""
import enum
import uuid
from datetime import datetime

from sqlalchemy import DateTime, Enum, ForeignKey, func
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.core.database import Base


class ScriptStatus(str, enum.Enum):
    running = "running"
    completed = "completed"
    killed = "killed"
    failed = "failed"


class ScriptExecution(Base):
    __tablename__ = "script_executions"

    id: Mapped[uuid.UUID] = mapped_column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    user_id: Mapped[uuid.UUID] = mapped_column(UUID(as_uuid=True), ForeignKey("users.id"), nullable=False)
    script_name: Mapped[str] = mapped_column(nullable=False)
    status: Mapped[ScriptStatus] = mapped_column(
        Enum(ScriptStatus, name="script_status"), nullable=False, default=ScriptStatus.running
    )
    pid: Mapped[int | None] = mapped_column(nullable=True)
    started_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now(), nullable=False)
    finished_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
    exit_code: Mapped[int | None] = mapped_column(nullable=True)
    output: Mapped[str | None] = mapped_column(nullable=True)
    error: Mapped[str | None] = mapped_column(nullable=True)

    user: Mapped["User"] = relationship("User", back_populates="script_executions")