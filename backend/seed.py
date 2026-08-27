"""
Seed the database with a demo dataset.

Idempotent: re-running updates nothing that already exists, so it is safe to
call after every `alembic upgrade head`.

    cd backend && .venv/bin/python seed.py

Creates:
  - admin@iitmandi.ac.in   (admin)      password Demo@1234
  - demo@iitmandi.ac.in    (member)     password Demo@1234
  - one Device with a FIXED uuid, so firmware config never has to change
  - one Room, 8 KeySlots, and a Permission granting the demo member access

Both demo users are pre-enrolled with a KNOWN TOTP secret. That is deliberate
for a demo — it lets you add the account to Google Authenticator by hand and
log in immediately, without walking the QR enrollment flow. Never seed a real
deployment with these values.
"""
from __future__ import annotations

import asyncio
import sys
import uuid
from datetime import datetime, timezone

from sqlalchemy import select

from app.core.database import AsyncSessionLocal
from app.core.security import get_totp_uri, hash_password
from app.models.device import Device
from app.models.key_slot import KeySlot, KeyStatus
from app.models.permission import Permission
from app.models.room import Room
from app.models.user import User, UserRole

# ── Demo constants ───────────────────────────────────────────────────────────
# Fixed so the firmware's DEVICE_UUID stays valid across re-seeds.
DEMO_DEVICE_ID = uuid.UUID("11111111-2222-3333-4444-555555555555")

DEMO_PASSWORD = "Demo@1234"

# Valid RFC 4648 base32. Shared by both demo users on purpose — one QR/secret
# to type in while testing.
DEMO_TOTP_SECRET = "JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP"

DEMO_USERS = [
    ("Demo Admin", "admin@iitmandi.ac.in", "B00000", UserRole.admin),
    ("Demo Member", "demo@iitmandi.ac.in", "B22101", UserRole.member),
]

DEMO_ROOMS = [
    ("SAC A-19", "SAC", "Student Activity Centre — robotics lab"),
    ("SAC A-20", "SAC", "Component store"),
]

# The carousel carries 24 keys. Must not exceed SLOT_COUNT in
# firmware/KMS_LowLevel_ArduinoUno/rack_geometry.h, nor the CHECK constraint on
# key_slots — seeding past either is an IntegrityError, not a silent truncation.
SLOT_COUNT = 24

# slot_number -> index into DEMO_ROOMS (None = unassigned spare).
# Slots 1-3 and 4-5 are assigned so the demo member has something to retrieve;
# the rest are left spare, which is also what a freshly built rack looks like.
SLOT_MAP = {n: None for n in range(1, SLOT_COUNT + 1)}
SLOT_MAP.update({1: 0, 2: 0, 3: 0, 4: 1, 5: 1})


async def seed(reset: bool = False) -> None:
    async with AsyncSessionLocal() as db:
        # ── Users ────────────────────────────────────────────────────────────
        users: dict[str, User] = {}
        for name, email, roll_no, role in DEMO_USERS:
            existing = (
                await db.execute(select(User).where(User.email == email))
            ).scalar_one_or_none()
            if existing:
                users[email] = existing
                if reset:
                    # Only touches password + TOTP. Role, roll_no and any
                    # permissions the account picked up are left alone.
                    existing.password_hash = hash_password(DEMO_PASSWORD)
                    existing.totp_secret = DEMO_TOTP_SECRET
                    existing.totp_enrolled_at = datetime.now(timezone.utc)
                    db.add(existing)
                    print(f"  ~ user reset: {email}")
                else:
                    print(f"  = user exists: {email} (--reset to restore demo creds)")
                continue
            user = User(
                name=name,
                email=email,
                roll_no=roll_no,
                role=role,
                password_hash=hash_password(DEMO_PASSWORD),
                totp_secret=DEMO_TOTP_SECRET,
                totp_enrolled_at=datetime.now(timezone.utc),
                is_active=True,
            )
            db.add(user)
            users[email] = user
            print(f"  + user: {email} ({role.value})")
        await db.commit()

        admin = users["admin@iitmandi.ac.in"]
        member = users["demo@iitmandi.ac.in"]

        # ── Rooms ────────────────────────────────────────────────────────────
        rooms: list[Room] = []
        for name, block, description in DEMO_ROOMS:
            existing = (
                await db.execute(select(Room).where(Room.name == name))
            ).scalar_one_or_none()
            if existing:
                rooms.append(existing)
                print(f"  = room exists: {name}")
                continue
            room = Room(
                name=name, block=block, description=description, coordinator_id=admin.id
            )
            db.add(room)
            rooms.append(room)
            print(f"  + room: {name}")
        await db.commit()

        # ── Device ───────────────────────────────────────────────────────────
        device = (
            await db.execute(select(Device).where(Device.id == DEMO_DEVICE_ID))
        ).scalar_one_or_none()
        if device:
            print(f"  = device exists: {DEMO_DEVICE_ID}")
        else:
            device = Device(
                id=DEMO_DEVICE_ID,
                name="SAC A-19 Enclosure",
                location="Student Activity Centre, ground floor",
                status="offline",
            )
            db.add(device)
            await db.commit()
            print(f"  + device: {DEMO_DEVICE_ID}")

        # ── Key slots ────────────────────────────────────────────────────────
        added_slots = 0
        for slot_number, room_idx in SLOT_MAP.items():
            existing = (
                await db.execute(
                    select(KeySlot).where(
                        KeySlot.device_id == device.id,
                        KeySlot.slot_number == slot_number,
                    )
                )
            ).scalar_one_or_none()
            if existing:
                continue
            db.add(
                KeySlot(
                    device_id=device.id,
                    slot_number=slot_number,
                    room_id=rooms[room_idx].id if room_idx is not None else None,
                    status=KeyStatus.available,
                )
            )
            added_slots += 1
        await db.commit()
        print(
            f"  + {added_slots} key slots on {device.name}"
            if added_slots
            else f"  = key slots already present on {device.name}"
        )

        # ── Permissions — demo member can access every seeded room ───────────
        added_perms = 0
        for room in rooms:
            existing = (
                await db.execute(
                    select(Permission).where(
                        Permission.user_id == member.id, Permission.room_id == room.id
                    )
                )
            ).scalar_one_or_none()
            if existing:
                continue
            db.add(
                Permission(user_id=member.id, room_id=room.id, granted_by=admin.id)
            )
            added_perms += 1
        await db.commit()
        print(
            f"  + {added_perms} permissions for demo member"
            if added_perms
            else "  = permissions already present"
        )

        # Report the secret each account actually holds. A pre-existing user
        # keeps its own secret unless --reset was passed, and printing the
        # constant regardless would hand you a code that never verifies.
        _print_summary(
            [(email, users[email].totp_secret) for _, email, _, _ in DEMO_USERS]
        )


def _print_summary(secrets: list[tuple[str, str | None]]) -> None:
    print("\n" + "─" * 64)
    print(" DEMO CREDENTIALS")
    print("─" * 64)
    print(f" Password (both accounts):  {DEMO_PASSWORD}\n")
    for email, secret in secrets:
        print(f" {email}")
        if not secret:
            print("   no TOTP secret — enroll via the QR flow at login\n")
            continue
        print(f"   secret:  {secret}")
        print(f"   uri:     {get_totp_uri(secret, email)}\n")
    print(" Google Authenticator -> + -> \"Enter a setup key\"")
    print("   paste the secret above, type = Time based\n")
    print(" DEVICE_UUID for firmware/kms_enclosure/config.h:")
    print(f"   {DEMO_DEVICE_ID}")
    print("─" * 64)
    print(" Demo secret is public. Do not seed production with it.")
    print("─" * 64 + "\n")


if __name__ == "__main__":
    asyncio.run(seed(reset="--reset" in sys.argv))
