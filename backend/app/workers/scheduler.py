"""APScheduler jobs for overdue notification scheduling."""
from __future__ import annotations

from datetime import datetime, timedelta, timezone

from apscheduler.schedulers.asyncio import AsyncIOScheduler
from apscheduler.triggers.interval import IntervalTrigger
from sqlalchemy import select

from app.core.config import get_settings
from app.core.database import AsyncSessionLocal
from app.models.retrieval_log import RetrievalLog, RetrievalStatus

settings = get_settings()

_scheduler = AsyncIOScheduler()


def start_scheduler() -> None:
    _scheduler.add_job(check_due_notifications, IntervalTrigger(minutes=1), id="due_notifications")
    _scheduler.start()


def stop_scheduler() -> None:
    _scheduler.shutdown(wait=False)


async def check_due_notifications() -> None:
    """Run every minute — sends reminders, overdue warnings, and escalations."""
    now = datetime.now(timezone.utc)
    reminder_threshold = now + timedelta(minutes=settings.reminder_before_due_minutes)
    escalation_threshold = now - timedelta(hours=settings.escalation_after_due_hours)

    async with AsyncSessionLocal() as db:
        r = await db.execute(
            select(RetrievalLog).where(
                RetrievalLog.status.in_((RetrievalStatus.active, RetrievalStatus.overdue))
            )
        )
        active_logs = r.scalars().all()

        from app.models.key_slot import KeySlot
        from app.models.room import Room
        from app.models.user import User
        from app.services.notification_service import NotificationService
        notifier = NotificationService(db)

        for log in active_logs:
            user_r = await db.execute(select(User).where(User.id == log.user_id))
            user = user_r.scalar_one_or_none()
            if not user:
                continue

            slot_r = await db.execute(select(KeySlot).where(KeySlot.id == log.key_slot_id))
            slot = slot_r.scalar_one_or_none()
            if not slot or not slot.room_id:
                continue

            room_r = await db.execute(select(Room).where(Room.id == slot.room_id))
            room = room_r.scalar_one_or_none()
            if not room:
                continue

            # Return reminder (once, T-30min)
            if (
                log.reminder_count == 0
                and log.due_at <= reminder_threshold
                and log.due_at > now
            ):
                await notifier.send_return_reminder(user, room, log.due_at)
                log.reminder_count = 1
                db.add(log)
                await db.commit()

            # Overdue warning (once, at due time)
            elif log.reminder_count == 1 and log.due_at <= now:
                await notifier.send_overdue_warning(user, room)
                log.reminder_count = 2
                log.status = RetrievalStatus.overdue
                db.add(log)
                await db.commit()

            # Coordinator escalation (FR-9, T+2h)
            elif log.reminder_count == 2 and log.due_at <= escalation_threshold:
                if room.coordinator_id:
                    coord_r = await db.execute(select(User).where(User.id == room.coordinator_id))
                    coordinator = coord_r.scalar_one_or_none()
                    if coordinator:
                        await notifier.send_coordinator_escalation(
                            coordinator, user, room, log.retrieved_at
                        )
                log.reminder_count = 3  # don't escalate again
                db.add(log)
                await db.commit()
