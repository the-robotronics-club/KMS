"""WebSocket Connection Manager for real-time updates."""
from __future__ import annotations
from fastapi import WebSocket

class ConnectionManager:
    def __init__(self):
        self.active_connections: list[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

    async def broadcast(self, message: dict | str):
        import json
        payload = json.dumps(message) if isinstance(message, dict) else message
        for connection in self.active_connections:
            try:
                await connection.send_text(payload)
            except Exception:
                # Connection might be closed, we will clean it up on disconnect
                pass

manager = ConnectionManager()
