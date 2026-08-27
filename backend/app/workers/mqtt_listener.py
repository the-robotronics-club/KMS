"""MQTT listener background worker — subscribes to device topics, writes events to DB."""
from __future__ import annotations

import asyncio
import json
import logging
import uuid
from datetime import datetime, timezone

from app.core.config import get_settings
from app.core.database import AsyncSessionLocal
from app.core.redis_client import get_redis, live_status_channel
from app.models.access_log import AccessLog
from app.models.device import Device
from app.models.override_log import OverrideLog, OverrideTrigger
from app.services.mqtt_service import mqtt_client
from app.services.proximity_service import ProximityService

settings = get_settings()
logger = logging.getLogger(__name__)


async def run_mqtt_listener() -> None:
    """Main MQTT listener loop — reconnects on failure."""
    while True:
        try:
            async with mqtt_client() as client:
                # Subscribe to all device topics
                await client.subscribe("device/+/access/event")
                await client.subscribe("device/+/access/proximity_code")
                await client.subscribe("device/+/access/tamper_event")
                await client.subscribe("device/+/rack/event")
                await client.subscribe("device/+/power/telemetry")
                await client.subscribe("device/+/+/heartbeat")

                async for message in client.messages:
                    try:
                        await _handle_message(str(message.topic), message.payload)
                    except Exception as e:
                        print(f"[MQTT] Error handling message: {e}")
        except Exception as e:
            print(f"[MQTT] Connection lost: {e}. Reconnecting in 5s...")
            await asyncio.sleep(5)


async def _handle_message(topic: str, payload: bytes) -> None:
    parts = topic.split("/")
    if len(parts) < 3:
        return

    device_id_str = parts[1]
    try:
        device_id = uuid.UUID(device_id_str)
    except ValueError:
        return

    try:
        data = json.loads(payload)
    except json.JSONDecodeError:
        return

    async with AsyncSessionLocal() as db:
        if "access/proximity_code" in topic:
            code = data.get("code")
            if code:
                svc = ProximityService()
                await svc.cache_proximity_code(code, device_id)

        elif "access/tamper_event" in topic:
            await _handle_tamper(db, device_id, data)

        elif "access/event" in topic:
            event = data.get("event", "unknown")
            log = AccessLog(
                device_id=device_id,
                event_type=f"door_{event.split('_')[-1]}",
                metadata_=data,
            )
            db.add(log)
            await db.commit()

        elif "rack/event" in topic:
            slot_number = data.get("slot_number") or data.get("slot_id")
            event = data.get("event", "unknown")
            log = AccessLog(
                device_id=device_id,
                event_type=f"slot_{event}",
                metadata_=data,
            )
            db.add(log)
            await db.commit()

            # Tell the browser the rack finished. Until this existed the chain
            # stopped here: the Uno reported DONE, the ESP32 published
            # "dispensed", this handler wrote a log row — and the page still
            # showed whatever it had, because nothing woke the WebSocket.
            #
            # The client refetches on any message rather than parsing the
            # payload (frontend/app/keys/page.tsx), so the body is for humans
            # reading Redis, not for the UI.
            try:
                redis = get_redis()
                await redis.publish(
                    live_status_channel(),
                    f"rack_{event}:{device_id}:{slot_number}",
                )
            except Exception:
                # A dead Redis must not stop the audit log from being written —
                # that already happened above, and it is the record that matters.
                logger.exception(
                    "Live-status publish failed for %s slot %s", device_id, slot_number
                )

        elif "power/telemetry" in topic:
            await _update_device_telemetry(db, device_id, data)

        elif "heartbeat" in topic:
            await _update_heartbeat(db, device_id, data)


async def _handle_tamper(db, device_id: uuid.UUID, data: dict) -> None:
    """Log tamper event and immediately email all admins."""
    override = OverrideLog(
        device_id=device_id,
        triggered_by=OverrideTrigger.physical,
        ts=datetime.now(timezone.utc),
    )
    db.add(override)
    await db.commit()

    # Get device name for alert
    from sqlalchemy import select
    dr = await db.execute(select(Device).where(Device.id == device_id))
    device = dr.scalar_one_or_none()
    device_name = device.name if device else str(device_id)

    # Email all admins
    from app.models.user import User, UserRole
    from app.services.notification_service import NotificationService
    ar = await db.execute(select(User).where(User.role == UserRole.admin, User.is_active == True))
    admins = ar.scalars().all()
    svc = NotificationService(db)
    for admin in admins:
        await svc.send_tamper_alert(admin, device_name, override.ts)


async def _update_device_telemetry(db, device_id: uuid.UUID, data: dict) -> None:
    from sqlalchemy import select, update
    await db.execute(
        update(Device).where(Device.id == device_id).values(
            battery_pct=data.get("battery_pct"),
            on_backup_power=data.get("on_backup", False),
            wifi_rssi=data.get("rssi"),
            status="online",
        )
    )
    await db.commit()


async def _update_heartbeat(db, device_id: uuid.UUID, data: dict) -> None:
    from sqlalchemy import update
    await db.execute(
        update(Device).where(Device.id == device_id).values(
            last_heartbeat_at=datetime.now(timezone.utc),
            firmware_version=data.get("firmware_version"),
            status="online",
        )
    )
    await db.commit()
