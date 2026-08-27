"use client";

import { useEffect, useState } from "react";
import { admin, type Device } from "@/lib/api";
import { formatDistanceToNow } from "date-fns";

export default function AdminDevicesPage() {
  const [devices, setDevices] = useState<Device[]>([]);
  const [loading, setLoading] = useState(true);
  const [showCreate, setShowCreate] = useState(false);
  const [form, setForm] = useState({ name: "", location: "" });

  useEffect(() => {
    fetchDevices();
    const interval = setInterval(fetchDevices, 30000);
    return () => clearInterval(interval);
  }, []);

  async function fetchDevices() {
    try { setDevices(await admin.listDevices()); } finally { setLoading(false); }
  }

  async function handleCreate(e: React.FormEvent) {
    e.preventDefault();
    await admin.createDevice(form);
    setShowCreate(false);
    setForm({ name: "", location: "" });
    fetchDevices();
  }

  async function handleMaintenance(deviceId: string) {
    await admin.toggleMaintenance(deviceId);
    fetchDevices();
  }

  return (
    <div>
      <div className="page-header">
        <div>
          <h1 className="page-title">📟 Device Health</h1>
          <p className="page-subtitle">Real-time hardware status and telemetry</p>
        </div>
        <div style={{ display: "flex", gap: "0.75rem" }}>
          <button onClick={fetchDevices} className="btn btn-ghost btn-sm">↻ Refresh</button>
          <button className="btn btn-primary btn-sm" onClick={() => setShowCreate(true)}>+ Add Device</button>
        </div>
      </div>

      {showCreate && (
        <div className="card" style={{ marginBottom: "1.5rem" }}>
          <h3 style={{ marginBottom: "1rem" }}>Register Device</h3>
          <form onSubmit={handleCreate} style={{ display: "flex", gap: "1rem", alignItems: "flex-end" }}>
            <div className="form-group" style={{ flex: 1 }}>
              <label className="label">Device Name *</label>
              <input className="input" value={form.name} onChange={e => setForm({ ...form, name: e.target.value })} required placeholder="SAC A-19 Enclosure" />
            </div>
            <div className="form-group" style={{ flex: 1 }}>
              <label className="label">Location</label>
              <input className="input" value={form.location} onChange={e => setForm({ ...form, location: e.target.value })} placeholder="SAC A-19" />
            </div>
            <button type="submit" className="btn btn-primary">Register</button>
            <button type="button" className="btn btn-ghost" onClick={() => setShowCreate(false)}>Cancel</button>
          </form>
        </div>
      )}

      {loading ? (
        <div style={{ textAlign: "center", padding: "3rem" }}>
          <div className="spinner" style={{ width: 36, height: 36, borderWidth: 3, margin: "auto" }} />
        </div>
      ) : (
        <div style={{ display: "grid", gridTemplateColumns: "repeat(auto-fill, minmax(340px, 1fr))", gap: "1.25rem" }}>
          {devices.map(device => {
            const battPct = device.battery_pct ?? 0;
            const battClass = battPct >= 50 ? "ok" : battPct >= 20 ? "low" : "critical";
            const heartbeat = device.last_heartbeat_at
              ? formatDistanceToNow(new Date(device.last_heartbeat_at), { addSuffix: true })
              : "Never";
            const isOffline = device.status === "offline";
            return (
              <div
                key={device.id}
                className="card"
                id={`device-${device.id}`}
                style={{ borderColor: isOffline ? "var(--c-danger)" : device.battery_pct && device.battery_pct < 20 ? "var(--c-warning)" : undefined }}
              >
                <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start", marginBottom: "1rem" }}>
                  <div>
                    <div style={{ fontWeight: 700, fontSize: "1.05rem", display: "flex", alignItems: "center", gap: "0.5rem" }}>
                      <span className={`dot dot--${isOffline ? "red" : "green"}`} />
                      {device.name}
                    </div>
                    <div style={{ fontSize: "0.8rem", color: "var(--c-text-muted)", marginTop: "0.25rem" }}>
                      {device.location || "Unknown location"}
                    </div>
                  </div>
                  <div style={{ display: "flex", flexDirection: "column", gap: "0.25rem", alignItems: "flex-end" }}>
                    <span className={`badge badge--${isOffline ? "overdue" : "available"}`}>
                      {device.status.toUpperCase()}
                    </span>
                    {device.on_backup_power && (
                      <span className="badge badge--retrieved">🔋 BACKUP PWR</span>
                    )}
                  </div>
                </div>

                <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "0.875rem", marginBottom: "1rem" }}>
                  <div>
                    <div className="device-stat-label">Battery</div>
                    <div className="device-stat-value">{device.battery_pct ?? "—"}%</div>
                    <div className="battery-bar" style={{ marginTop: "0.375rem" }}>
                      <div className={`battery-fill battery-fill--${battClass}`} style={{ width: `${battPct}%` }} />
                    </div>
                  </div>
                  <div>
                    <div className="device-stat-label">WiFi Signal</div>
                    <div className="device-stat-value">{device.wifi_rssi ?? "—"} dBm</div>
                  </div>
                  <div>
                    <div className="device-stat-label">Firmware</div>
                    <div className="device-stat-value">{device.firmware_version ?? "—"}</div>
                  </div>
                  <div>
                    <div className="device-stat-label">Last Heartbeat</div>
                    <div className="device-stat-value" style={{ fontSize: "0.75rem" }}>{heartbeat}</div>
                  </div>
                </div>

                {isOffline && (
                  <div style={{ padding: "0.5rem 0.75rem", background: "var(--c-danger-glow)", border: "1px solid var(--c-danger)", borderRadius: "var(--radius-sm)", fontSize: "0.8rem", color: "var(--c-danger)", marginBottom: "0.75rem" }}>
                    ⚠️ Device is offline — check network connectivity
                  </div>
                )}
                {device.battery_pct && device.battery_pct < 20 && !isOffline && (
                  <div style={{ padding: "0.5rem 0.75rem", background: "var(--c-warning-glow)", border: "1px solid var(--c-warning)", borderRadius: "var(--radius-sm)", fontSize: "0.8rem", color: "var(--c-warning)", marginBottom: "0.75rem" }}>
                    ⚡ Low battery — charge or switch to backup power
                  </div>
                )}

                <button
                  className="btn btn-ghost btn-sm"
                  onClick={() => handleMaintenance(device.id)}
                >
                  🔧 Toggle Maintenance Mode
                </button>
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
}
