"use client";

import { useEffect, useState } from "react";
import { admin, keys, type KeySlot } from "@/lib/api";
import { formatDistanceToNow, isPast } from "date-fns";

export default function AdminMonitoringPage() {
  const [slots, setSlots] = useState<KeySlot[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetchSlots();
    const interval = setInterval(fetchSlots, 10000);
    return () => clearInterval(interval);
  }, []);

  async function fetchSlots() {
    try { setSlots(await keys.list()); } finally { setLoading(false); }
  }

  const retrieved = slots.filter(s => s.status === "retrieved");
  const overdue = retrieved.filter(s => s.due_at && isPast(new Date(s.due_at)));

  return (
    <div>
      <div className="page-header">
        <div>
          <h1 className="page-title">📡 Live Monitoring</h1>
          <p className="page-subtitle">Real-time key possession board — refreshes every 10 seconds</p>
        </div>
        <div style={{ display: "flex", gap: "0.75rem", alignItems: "center" }}>
          <span className="badge badge--available">
            <span className="dot dot--green" /> Live
          </span>
          <button onClick={fetchSlots} className="btn btn-ghost btn-sm">↻ Refresh</button>
        </div>
      </div>

      <div className="stat-grid" style={{ marginBottom: "2rem" }}>
        <div className="stat-card stat-card--warning">
          <div className="stat-icon">🗝️</div>
          <div className="stat-value">{retrieved.length}</div>
          <div className="stat-label">Keys Out</div>
        </div>
        <div className="stat-card stat-card--success">
          <div className="stat-icon">✅</div>
          <div className="stat-value">{slots.filter(s => s.status === "available").length}</div>
          <div className="stat-label">Available</div>
        </div>
        <div className="stat-card stat-card--danger">
          <div className="stat-icon">⏰</div>
          <div className="stat-value">{overdue.length}</div>
          <div className="stat-label">Overdue</div>
        </div>
        <div className="stat-card">
          <div className="stat-icon">🔧</div>
          <div className="stat-value">{slots.filter(s => s.status === "maintenance").length}</div>
          <div className="stat-label">Maintenance</div>
        </div>
      </div>

      {loading ? (
        <div style={{ textAlign: "center", padding: "3rem" }}>Loading...</div>
      ) : retrieved.length === 0 ? (
        <div className="card" style={{ textAlign: "center", padding: "3rem" }}>
          <div style={{ fontSize: "2.5rem", marginBottom: "1rem" }}>✅</div>
          <p style={{ color: "var(--c-text-muted)" }}>All keys are currently in the enclosure.</p>
        </div>
      ) : (
        <div className="table-wrapper">
          <table>
            <thead>
              <tr>
                <th>Room</th>
                <th>Slot</th>
                <th>Held By</th>
                <th>Due</th>
                <th>Status</th>
              </tr>
            </thead>
            <tbody>
              {retrieved.map(slot => {
                const isOverdue = slot.due_at && isPast(new Date(slot.due_at));
                return (
                  <tr
                    key={slot.slot_id}
                    style={{ background: isOverdue ? "rgba(239,68,68,0.05)" : undefined }}
                  >
                    <td style={{ fontWeight: 600 }}>{slot.room_name || "—"}</td>
                    <td className="font-mono" style={{ fontSize: "0.8rem" }}>#{slot.slot_number}</td>
                    <td>{slot.current_holder || "Unknown"}</td>
                    <td className="font-mono" style={{ fontSize: "0.8rem", color: isOverdue ? "var(--c-danger)" : "var(--c-warning)" }}>
                      {slot.due_at ? formatDistanceToNow(new Date(slot.due_at), { addSuffix: true }) : "—"}
                    </td>
                    <td>
                      <span className={`badge badge--${isOverdue ? "overdue" : "retrieved"}`}>
                        {isOverdue ? "OVERDUE" : "OUT"}
                      </span>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}
