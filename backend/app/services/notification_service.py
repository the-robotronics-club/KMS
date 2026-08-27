"""Notification service — sends emails via the provider-pattern email module."""
from __future__ import annotations

from datetime import datetime, timezone

from sqlalchemy.ext.asyncio import AsyncSession

from app.email.base import EmailMessage
from app.email.factory import get_email_provider
from app.email.templates import (
    coordinator_escalation_html,
    overdue_warning_html,
    retrieval_confirmation_html,
    return_reminder_html,
    tamper_alert_html,
)
from app.models.notification import Notification
from app.models.room import Room
from app.models.user import User


class NotificationService:
    def __init__(self, db: AsyncSession):
        self.db = db
        self.provider = get_email_provider()

    async def send_retrieval_confirmation(self, user: User, room: Room, due_at: datetime) -> None:
        """FR-8 — fires on successful retrieve."""
        msg = EmailMessage(
            to=user.email,
            subject=f"[SNTC] Key Retrieved: {room.name}",
            html_body=retrieval_confirmation_html(user.name, room.name, due_at),
        )
        await self.provider.send(msg)
        await self._record(user.id, "retrieval_confirmation", f"Key for {room.name} due at {due_at}")

    async def send_return_reminder(self, user: User, room: Room, due_at: datetime) -> None:
        """Fires ~30 min before due."""
        msg = EmailMessage(
            to=user.email,
            subject=f"[SNTC] Reminder: Return Key for {room.name}",
            html_body=return_reminder_html(user.name, room.name, due_at),
        )
        await self.provider.send(msg)
        await self._record(user.id, "return_reminder", f"Key for {room.name}")

    async def send_overdue_warning(self, user: User, room: Room) -> None:
        """Fires at due time (failure 1)."""
        msg = EmailMessage(
            to=user.email,
            subject=f"[SNTC] OVERDUE: Key for {room.name} not returned",
            html_body=overdue_warning_html(user.name, room.name),
        )
        await self.provider.send(msg)
        await self._record(user.id, "overdue_warning", f"Key for {room.name} overdue")

    async def send_coordinator_escalation(
        self, coordinator: User, member: User, room: Room, retrieved_at: datetime
    ) -> None:
        """Fires T+2h past due (failure 2)."""
        msg = EmailMessage(
            to=coordinator.email,
            subject=f"[SNTC] Escalation: Key for {room.name} still not returned",
            html_body=coordinator_escalation_html(
                coordinator.name, member.name, member.email, room.name, retrieved_at
            ),
        )
        await self.provider.send(msg)
        await self._record(coordinator.id, "coordinator_escalation", f"Escalation: {room.name}")

    async def send_tamper_alert(self, admin: User, device_name: str, ts: datetime) -> None:
        """Immediate alert on outer-box tamper event."""
        msg = EmailMessage(
            to=admin.email,
            subject=f"[SNTC] ALERT: Physical Override on {device_name}",
            html_body=tamper_alert_html(device_name, ts),
        )
        await self.provider.send(msg)

    async def _record(self, user_id, type_: str, message: str) -> None:
        notif = Notification(
            user_id=user_id,
            type=type_,
            message=message,
            channel="email",
            sent_at=datetime.now(timezone.utc),
        )
        self.db.add(notif)
        await self.db.commit()
