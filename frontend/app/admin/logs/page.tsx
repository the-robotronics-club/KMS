"use client";

import { useEffect, useState } from "react";
import { admin, type OverrideLog, type AccessLog, type RetrievalLog } from "@/lib/api";
import { format } from "date-fns";

type Tab = "access" | "retrieval" | "override";

export default function AdminLogsPage() {
  const [tab, setTab] = useState<Tab>("override");
  const [accessLogs, setAccessLogs] = useState<AccessLog[]>([]);
  const [retrievalLogs, setRetrievalLogs] = useState<RetrievalLog[]>([]);
  const [overrideLogs, setOverrideLogs] = useState<OverrideLog[]>([]);
  const [loading, setLoading] = useState(true);
  const [resolveId, setResolveId] = useState<string | null>(null);
  const [resolveNote, setResolveNote] = useState("");
  const [resolving, setResolving] = useState(false);

  useEffect(() => { fetchAll(); }, []);

  async function fetchAll() {
    setLoading(true);
    const [a, r, o] = await Promise.all([
      admin.accessLogs(),
      admin.retrievalLogs(),
      admin.overrideLogs(),
    ]);
    setAccessLogs(a); setRetrievalLogs(r); setOverrideLogs(o);
    setLoading(false);
  }

  async function handleResolve(id: string) {
    if (!resolveNote.trim()) return;
    setResolving(true);
    try {
      await admin.resolveOverride(id, resolveNote);
      setResolveId(null); setResolveNote("");
      fetchAll();
    } finally { setResolving(false); }
  }

  async function exportCSV(type: Tab) {
    const data = type === "access" ? accessLogs : type === "retrieval" ? retrievalLogs : overrideLogs;
    const keys = Object.keys(data[0] || {});
    const csv = [keys.join(","), ...data.map(row => keys.map(k => JSON.stringify((row as any)[k] ?? "")).join(","))].join("\n");
    const blob = new Blob([csv], { type: "text/csv" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a"); a.href = url; a.download = `skss-${type}-logs.csv`; a.click();
  }

  const tabs: { id: Tab; label: string; count?: number; warn?: boolean }[] = [
    { id: "override", label: "⚠️ Override / Tamper", count: overrideLogs.filter(l => !l.resolved_at).length, warn: true },
    { id: "retrieval", label: "🔑 Retrieval Logs", count: retrievalLogs.length },
    { id: "access", label: "🔐 Access Logs", count: accessLogs.length },
  ];

  return (
    <div>
      <div className="page-header">
        <div>
          <h1 className="page-title">📋 Audit Logs</h1>
          <p className="page-subtitle">Complete tamper-evident event history</p>
        </div>
        <button className="btn btn-ghost btn-sm" onClick={() => exportCSV(tab)}>📥 Export CSV</button>
      </div>

      {/* Tabs */}
      <div style={{ display: "flex", gap: "0.5rem", marginBottom: "1.5rem", borderBottom: "1px solid var(--c-border)", paddingBottom: "0.5rem" }}>
        {tabs.map(t => (
          <button
            key={t.id}
            onClick={() => setTab(t.id)}
            style={{
              background: tab === t.id ? "var(--c-primary-glow)" : "none",
              color: tab === t.id ? "var(--c-primary)" : t.warn && (t.count || 0) > 0 ? "var(--c-danger)" : "var(--c-text-muted)",
              border: tab === t.id ? "1px solid var(--c-primary)" : "1px solid transparent",
              borderRadius: "var(--radius-sm)",
              padding: "0.5rem 1rem",
              cursor: "pointer",
              fontSize: "0.875rem",
              fontWeight: 500,
            }}
          >
            {t.label}
            {(t.count || 0) > 0 && (
              <span style={{
                background: t.warn && (t.count || 0) > 0 ? "var(--c-danger)" : "var(--c-border)",
                color: "white",
                borderRadius: "999px",
                padding: "0.1rem 0.4rem",
                fontSize: "0.7rem",
                marginLeft: "0.5rem",
              }}>
                {t.count}
              </span>
            )}
          </button>
        ))}
      </div>

      {loading ? (
        <div style={{ textAlign: "center", padding: "3rem", color: "var(--c-text-muted)" }}>Loading...</div>
      ) : (
        <>
          {/* Override/Tamper Tab */}
          {tab === "override" && (
            <div>
              {overrideLogs.filter(l => !l.resolved_at).length > 0 && (
                <div className="tamper-alert" style={{ marginBottom: "1.5rem" }}>
                  <strong>🚨 {overrideLogs.filter(l => !l.resolved_at).length} unresolved tamper event(s) require your attention.</strong>
                  <p style={{ fontSize: "0.85rem", marginTop: "0.25rem", color: "var(--c-text-muted)" }}>
                    Add a resolution note to clear each flag.
                  </p>
                </div>
              )}
              <div className="table-wrapper">
                <table>
                  <thead>
                    <tr>
                      <th>Time</th>
                      <th>Device</th>
                      <th>Trigger</th>
                      <th>Status</th>
                      <th>Resolution</th>
                      <th>Action</th>
                    </tr>
                  </thead>
                  <tbody>
                    {overrideLogs.map(log => (
                      <tr key={log.id} id={`override-${log.id}`} style={!log.resolved_at ? { background: "rgba(239,68,68,0.05)" } : undefined}>
                        <td className="font-mono" style={{ fontSize: "0.8rem" }}>{format(new Date(log.ts), "MMM d, HH:mm:ss")}</td>
                        <td>{log.device_id.slice(0, 8)}...</td>
                        <td><span className={`badge badge--${log.triggered_by === "physical" ? "overdue" : "retrieved"}`}>{log.triggered_by.toUpperCase()}</span></td>
                        <td>
                          {log.resolved_at
                            ? <span className="badge badge--available">✓ Resolved</span>
                            : <span className="badge badge--overdue">🚨 OPEN</span>}
                        </td>
                        <td style={{ fontSize: "0.8rem", color: "var(--c-text-muted)" }}>{log.resolution_note || "—"}</td>
                        <td>
                          {!log.resolved_at && (
                            resolveId === log.id ? (
                              <div style={{ display: "flex", gap: "0.5rem", alignItems: "center" }}>
                                <input
                                  className="input"
                                  style={{ padding: "0.375rem 0.625rem", fontSize: "0.8rem" }}
                                  placeholder="Resolution note..."
                                  value={resolveNote}
                                  onChange={e => setResolveNote(e.target.value)}
                                />
                                <button
                                  className="btn btn-success btn-sm"
                                  disabled={resolving || !resolveNote.trim()}
                                  onClick={() => handleResolve(log.id)}
                                >
                                  {resolving ? "..." : "Save"}
                                </button>
                                <button className="btn btn-ghost btn-sm" onClick={() => setResolveId(null)}>×</button>
                              </div>
                            ) : (
                              <button className="btn btn-danger btn-sm" onClick={() => setResolveId(log.id)}>
                                Resolve
                              </button>
                            )
                          )}
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </div>
          )}

          {/* Retrieval Tab */}
          {tab === "retrieval" && (
            <div className="table-wrapper">
              <table>
                <thead>
                  <tr>
                    <th>Retrieved At</th>
                    <th>Due At</th>
                    <th>Returned At</th>
                    <th>Status</th>
                    <th>Extensions</th>
                    <th>Reminders Sent</th>
                  </tr>
                </thead>
                <tbody>
                  {retrievalLogs.map(log => (
                    <tr key={log.id}>
                      <td className="font-mono" style={{ fontSize: "0.8rem" }}>{format(new Date(log.retrieved_at), "MMM d, HH:mm")}</td>
                      <td className="font-mono" style={{ fontSize: "0.8rem" }}>{format(new Date(log.due_at), "MMM d, HH:mm")}</td>
                      <td className="font-mono" style={{ fontSize: "0.8rem" }}>{log.returned_at ? format(new Date(log.returned_at), "MMM d, HH:mm") : "—"}</td>
                      <td><span className={`badge badge--${log.status === "returned" ? "available" : log.status === "overdue" ? "overdue" : "retrieved"}`}>{log.status}</span></td>
                      <td>{log.extension_count}</td>
                      <td>{log.reminder_count}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}

          {/* Access Tab */}
          {tab === "access" && (
            <div className="table-wrapper">
              <table>
                <thead>
                  <tr>
                    <th>Time</th>
                    <th>Event</th>
                    <th>User</th>
                    <th>Device</th>
                    <th>Metadata</th>
                  </tr>
                </thead>
                <tbody>
                  {accessLogs.map(log => (
                    <tr key={log.id}>
                      <td className="font-mono" style={{ fontSize: "0.8rem" }}>{format(new Date(log.ts), "MMM d, HH:mm:ss")}</td>
                      <td>
                        <span style={{
                          color: log.event_type.includes("fail") ? "var(--c-danger)"
                            : log.event_type.includes("success") ? "var(--c-success)"
                            : "var(--c-text-muted)",
                          fontSize: "0.8rem",
                          fontFamily: "var(--font-mono, monospace)",
                        }}>
                          {log.event_type}
                        </span>
                      </td>
                      <td style={{ fontSize: "0.8rem" }}>{log.user_id?.slice(0, 8) || "—"}</td>
                      <td style={{ fontSize: "0.8rem" }}>{log.device_id?.slice(0, 8) || "—"}</td>
                      <td style={{ fontSize: "0.75rem", color: "var(--c-text-dim)", maxWidth: "200px" }} className="truncate">
                        {log.metadata_ ? JSON.stringify(log.metadata_) : "—"}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </>
      )}
    </div>
  );
}
