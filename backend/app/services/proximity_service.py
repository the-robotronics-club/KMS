"""Proximity service — validates device-issued codes, manages proximity flag."""
import uuid

from app.core.config import get_settings
from app.core.redis_client import get_redis, proximity_code_key
from app.core.security import check_proximity_flag, set_proximity_flag
from app.schemas import ProximityVerifyRequest, ProximityVerifyResponse

settings = get_settings()


class ProximityService:
    async def verify_code(
        self, req: ProximityVerifyRequest, session_id: str
    ) -> ProximityVerifyResponse:
        redis = get_redis()
        key = proximity_code_key(req.code)
        stored_device_id = await redis.get(key)

        if not stored_device_id:
            raise ValueError("Proximity code invalid or expired.")

        # The code itself identifies the device. device_id is only cross-checked
        # when the caller supplied one (captive-portal redirect carries it).
        if req.device_id and uuid.UUID(stored_device_id) != req.device_id:
            raise ValueError("Proximity code does not match the given device.")

        # The final GETDEL is the single-use gate. Multiple requests may pass
        # the read above, but only one can consume the code successfully.
        consumed_device_id = await redis.getdel(key)
        if consumed_device_id != stored_device_id:
            raise ValueError("Proximity code invalid or expired.")

        # Set the proximity-verified flag on this session
        await set_proximity_flag(session_id, stored_device_id)

        return ProximityVerifyResponse(
            proximity_verified=True,
            expires_in_seconds=settings.proximity_flag_ttl_seconds,
        )

    async def is_proximity_verified(self, session_id: str) -> bool:
        result = await check_proximity_flag(session_id)
        return result is not None

    async def get_verified_device_id(self, session_id: str) -> uuid.UUID | None:
        result = await check_proximity_flag(session_id)
        return uuid.UUID(result) if result else None

    async def cache_proximity_code(self, code: str, device_id: uuid.UUID) -> None:
        """Called by MQTT listener when a new proximity code arrives from a device."""
        redis = get_redis()
        await redis.setex(
            proximity_code_key(code),
            settings.proximity_code_ttl_seconds,
            str(device_id),
        )
