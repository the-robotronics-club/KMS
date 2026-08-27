"""MQTT service — publish commands with nonce-based replay protection."""
import json
import uuid
from datetime import datetime, timezone

from app.core.config import get_settings
from app.core.security import consume_nonce, generate_nonce

settings = get_settings()


def mqtt_client():
    """
    Build a client from settings.

    Both the publisher here and the listener in app.workers.mqtt_listener go
    through this, so TLS can't end up enabled on one side and forgotten on the
    other — which is a connection that fails only on the command path.
    """
    import aiomqtt

    return aiomqtt.Client(
        hostname=settings.mqtt_host,
        port=settings.mqtt_port,
        username=settings.mqtt_username or None,
        password=settings.mqtt_password or None,
        # TLSParameters() with every field None means "use the system CA bundle",
        # which is what HiveMQ Cloud's Let's Encrypt chain verifies against.
        # Passing nothing leaves the socket plaintext and 8883 just drops it.
        tls_params=aiomqtt.TLSParameters() if settings.mqtt_tls else None,
    )


class MQTTService:
    """Thin wrapper around aiomqtt publish operations."""

    async def _publish(self, topic: str, payload: dict) -> None:
        async with mqtt_client() as client:
            await client.publish(topic, payload=json.dumps(payload), qos=1)

    async def unlock_door(self, device_id: uuid.UUID, session_id: str) -> str:
        """Publish an unlock_door command to the Access Controller."""
        nonce = generate_nonce()
        if not await consume_nonce(nonce):
            raise RuntimeError("Nonce collision — retry.")
        topic = f"device/{device_id}/access/command"
        await self._publish(topic, {
            "action": "unlock_door",
            "session_id": session_id,
            "nonce": nonce,
            "ttl_s": 30,
            "ts": datetime.now(timezone.utc).isoformat(),
        })
        return nonce

    async def dispense_slot(self, device_id: uuid.UUID, slot_number: int) -> str:
        """Publish a dispense command to the Rack Controller."""
        nonce = generate_nonce()
        if not await consume_nonce(nonce):
            raise RuntimeError("Nonce collision — retry.")
        topic = f"device/{device_id}/rack/command"
        await self._publish(topic, {
            "action": "dispense",
            "slot_number": slot_number,
            "nonce": nonce,
            "ts": datetime.now(timezone.utc).isoformat(),
        })
        return nonce

    async def unlock_slot(self, device_id: uuid.UUID, slot_number: int) -> str:
        """Publish an unlock (return) command to the Rack Controller."""
        nonce = generate_nonce()
        if not await consume_nonce(nonce):
            raise RuntimeError("Nonce collision — retry.")
        topic = f"device/{device_id}/rack/command"
        await self._publish(topic, {
            "action": "unlock",
            "slot_number": slot_number,
            "nonce": nonce,
            "ts": datetime.now(timezone.utc).isoformat(),
        })
        return nonce
