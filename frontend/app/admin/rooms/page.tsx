"use client";

import { useEffect, useState } from "react";
import { admin, type Room } from "@/lib/api";

export default function AdminRoomsPage() {
  const [rooms, setRooms] = useState<Room[]>([]);
  const [loading, setLoading] = useState(true);
  const [showCreate, setShowCreate] = useState(false);
  const [form, setForm] = useState({ name: "", block: "", description: "", coordinator_id: "" });

  useEffect(() => { admin.listRooms().then(setRooms).finally(() => setLoading(false)); }, []);

  async function handleCreate(e: React.FormEvent) {
    e.preventDefault();
    await admin.createRoom({ ...form, coordinator_id: form.coordinator_id || undefined });
    setShowCreate(false);
    setForm({ name: "", block: "", description: "", coordinator_id: "" });
    admin.listRooms().then(setRooms);
  }

  return (
    <div>
      <div className="page-header">
        <div>
          <h1 className="page-title">🏛️ Rooms & Key Slots</h1>
          <p className="page-subtitle">{rooms.length} rooms configured</p>
        </div>
        <button className="btn btn-primary btn-sm" onClick={() => setShowCreate(true)}>+ Add Room</button>
      </div>

      {showCreate && (
        <div className="card" style={{ marginBottom: "1.5rem" }}>
          <h3 style={{ marginBottom: "1rem" }}>Add Room</h3>
          <form onSubmit={handleCreate} style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "1rem" }}>
            <div className="form-group">
              <label className="label">Room Name *</label>
              <input className="input" value={form.name} onChange={e => setForm({ ...form, name: e.target.value })} required placeholder="Electronics Lab" />
            </div>
            <div className="form-group">
              <label className="label">Block</label>
              <input className="input" value={form.block} onChange={e => setForm({ ...form, block: e.target.value })} placeholder="A-19" />
            </div>
            <div className="form-group" style={{ gridColumn: "1 / -1" }}>
              <label className="label">Description</label>
              <input className="input" value={form.description} onChange={e => setForm({ ...form, description: e.target.value })} />
            </div>
            <div style={{ gridColumn: "1 / -1", display: "flex", gap: "0.75rem" }}>
              <button type="submit" className="btn btn-primary">Create Room</button>
              <button type="button" className="btn btn-ghost" onClick={() => setShowCreate(false)}>Cancel</button>
            </div>
          </form>
        </div>
      )}

      {loading ? (
        <div style={{ textAlign: "center", padding: "3rem" }}>Loading...</div>
      ) : (
        <div style={{ display: "grid", gridTemplateColumns: "repeat(auto-fill, minmax(280px, 1fr))", gap: "1rem" }}>
          {rooms.map(room => (
            <div key={room.id} className="card card--glow" id={`room-${room.id}`}>
              <h3 style={{ marginBottom: "0.5rem" }}>{room.name}</h3>
              {room.block && <div style={{ fontSize: "0.8rem", color: "var(--c-text-muted)", marginBottom: "0.5rem" }}>📍 {room.block}</div>}
              {room.description && <p style={{ fontSize: "0.85rem", color: "var(--c-text-muted)", marginBottom: "0.75rem" }}>{room.description}</p>}
              <div style={{ fontSize: "0.75rem", color: "var(--c-text-dim)", fontFamily: "var(--font-mono, monospace)" }}>
                ID: {room.id.slice(0, 8)}...
              </div>
              {room.coordinator_id && (
                <div style={{ fontSize: "0.8rem", color: "var(--c-accent)", marginTop: "0.5rem" }}>
                  👤 Coordinator assigned
                </div>
              )}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
