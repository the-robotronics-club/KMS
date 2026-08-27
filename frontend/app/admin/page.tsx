"use client";

import { useEffect, useState } from "react";
import { admin, type DashboardSummary } from "@/lib/api";
import { formatDistanceToNow } from "date-fns";

export default function AdminDashboardPage() {
  const [data, setData] = useState<DashboardSummary | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    admin.dashboard().then(setData).finally(() => setLoading(false));
    const interval = setInterval(() => admin.dashboard().then(setData), 30000);
    return () => clearInterval(interval);
  }, []);

  return (
    <div>
      <div className="page-header">
        <div>
          <p className="eyebrow">Operations / Overview</p>
          <h1 className="page-title">Control room</h1>
          <p className="page-subtitle">Live status across keys, devices, and access.</p>
        </div>
        <button onClick={() => admin.dashboard().then(setData)} className="btn btn-ghost btn-sm">
          Refresh data
        </button>
      </div>

      {loading ? (
        <div>
          <div className="stat-grid" style={{ marginBottom: "2rem" }}>
            {[1, 2, 3, 4].map((i) => (
              <div key={i} className="stat-card skeleton" style={{ height: 100 }} />
            ))}
          </div>
          <div style={{ display: "grid", gridTemplateColumns: "repeat(auto-fill, minmax(280px, 1fr))", gap: "1rem" }}>
            {[1, 2, 3].map((i) => (
              <div key={i} className="device-card skeleton" style={{ height: 120 }} />
            ))}
          </div>
        </div>
      ) : data ? (
        <>
          {data.unresolved_tamper_count > 0 && (
            <div className="tamper-alert" style={{ marginBottom: "1.5rem" }}>
              <div style={{ display: "flex", alignItems: "center", gap: "1rem" }}>
                <span className="alert-mark">!</span>
                <div>
                  <h3 style={{ color: "var(--c-danger)", fontWeight: 700, marginBottom: "0.25rem" }}>
                    {data.unresolved_tamper_count} Unresolved Tamper Event{data.unresolved_tamper_count > 1 ? "s" : ""}
                  </h3>
                  <p style={{ fontSize: "0.875rem", color: "var(--c-text-muted)" }}>
                    Physical outer box was opened. Review and add a resolution note in the Override Logs.
                  </p>
                </div>
                <a href="/admin/logs?tab=override" className="btn btn-danger btn-sm" style={{ marginLeft: "auto", flexShrink: 0 }}>
                  Review Now →
                </a>
              </div>
            </div>
          )}

          <div className="stat-grid" style={{ marginBottom: "2rem" }}>
            <div className="stat-card stat-card--warning">
              <div className="stat-index">01</div>
              <div className="stat-value">{data.keys_out}</div>
              <div className="stat-label">Keys Currently Out</div>
            </div>
            <div className="stat-card stat-card--danger">
              <div className="stat-index">02</div>
              <div className="stat-value">{data.overdue_count}</div>
              <div className="stat-label">Overdue Keys</div>
            </div>
            <div className="stat-card stat-card--primary">
              <div className="stat-index">03</div>
              <div className="stat-value">{data.today_retrieval_count}</div>
              <div className="stat-label">Retrievals Today</div>
            </div>
            <div className="stat-card stat-card--success">
              <div className="stat-index">04</div>
              <div className="stat-value">{data.device_summary.filter(d => d.status === "online").length}/{data.device_summary.length}</div>
              <div className="stat-label">Devices Online</div>
            </div>
          </div>

          <div className="section-heading"><h2>Device health</h2><span>{data.device_summary.length} registered</span></div>
          <div className="admin-device-grid">
            {data.device_summary.map((device) => {
              const battPct = device.battery_pct ?? 0;
              const battClass = battPct >= 50 ? "ok" : battPct >= 20 ? "low" : "critical";
              const heartbeat = device.last_heartbeat_at
                ? formatDistanceToNow(new Date(device.last_heartbeat_at), { addSuffix: true })
                : "Never";
              return (
                <div key={device.id} className="device-card">
                  <div className="device-name">
                    <span className={`dot dot--${device.status === "online" ? "green" : "red"}`} />
                    {device.name}
                    {device.on_backup_power && (
                      <span className="device-backup">BACKUP POWER</span>
                    )}
                  </div>
                  <div className="device-stats">
                    <div className="device-stat-item">
                      <span className="device-stat-label">Battery</span>
                      <span className="device-stat-value">{device.battery_pct ?? "—"}%</span>
                      <div className="battery-bar">
                        <div className={`battery-fill battery-fill--${battClass}`} style={{ width: `${battPct}%` }} />
                      </div>
                    </div>
                    <div className="device-stat-item">
                      <span className="device-stat-label">WiFi</span>
                      <span className="device-stat-value">{device.wifi_rssi ?? "—"} dBm</span>
                    </div>
                    <div className="device-stat-item">
                      <span className="device-stat-label">Firmware</span>
                      <span className="device-stat-value">{device.firmware_version ?? "—"}</span>
                    </div>
                    <div className="device-stat-item">
                      <span className="device-stat-label">Heartbeat</span>
                      <span className="device-stat-value" style={{ fontSize: "0.75rem" }}>{heartbeat}</span>
                    </div>
                  </div>
                </div>
              );
            })}
          </div>

          <div className="section-heading"><h2>Shortcuts</h2><span>Common tasks</span></div>
          <div className="quick-actions">
            <a href="/admin/monitoring" className="btn btn-primary">Live monitoring</a>
            <a href="/admin/logs" className="btn btn-ghost">Audit logs</a>
            <a href="/admin/users" className="btn btn-ghost">Manage users</a>
            <a href="/admin/reports" className="btn btn-ghost">Reports</a>
          </div>
        </>
      ) : (
        <p style={{ color: "var(--c-text-muted)" }}>Failed to load dashboard.</p>
      )}
    </div>
  );
}
