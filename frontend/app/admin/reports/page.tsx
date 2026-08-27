"use client";

import { useEffect, useState } from "react";
import { admin } from "@/lib/api";
import { BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, CartesianGrid } from "recharts";
import { format } from "date-fns";

export default function AdminReportsPage() {
  const [report, setReport] = useState<{ rows: any[]; generated_at: string } | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    admin.usageReport().then(setReport).finally(() => setLoading(false));
  }, []);

  return (
    <div>
      <div className="page-header">
        <div>
          <h1 className="page-title">📈 Usage Reports</h1>
          <p className="page-subtitle">
            {report ? `Generated ${format(new Date(report.generated_at), "MMM d, HH:mm")}` : ""}
          </p>
        </div>
        <button
          className="btn btn-ghost btn-sm"
          onClick={() => admin.usageReport().then(setReport)}
        >
          ↻ Refresh
        </button>
      </div>

      {loading ? (
        <div style={{ textAlign: "center", padding: "3rem" }}>
          <div className="spinner" style={{ width: 36, height: 36, borderWidth: 3, margin: "auto" }} />
        </div>
      ) : report && report.rows.length > 0 ? (
        <>
          {/* Summary Stats */}
          <div className="stat-grid" style={{ marginBottom: "2rem" }}>
            <div className="stat-card stat-card--primary">
              <div className="stat-icon">📊</div>
              <div className="stat-value">{report.rows.reduce((s, r) => s + r.total_retrievals, 0)}</div>
              <div className="stat-label">Total Retrievals</div>
            </div>
            <div className="stat-card stat-card--danger">
              <div className="stat-icon">⏰</div>
              <div className="stat-value">{report.rows.reduce((s, r) => s + r.overdue_count, 0)}</div>
              <div className="stat-label">Total Overdue</div>
            </div>
            <div className="stat-card stat-card--success">
              <div className="stat-icon">⌛</div>
              <div className="stat-value">
                {(report.rows.reduce((s, r) => s + r.avg_possession_minutes, 0) / (report.rows.length || 1)).toFixed(0)}m
              </div>
              <div className="stat-label">Avg Possession Time</div>
            </div>
          </div>

          {/* Chart */}
          <div className="card" style={{ marginBottom: "2rem" }}>
            <h3 style={{ marginBottom: "1.25rem" }}>Retrievals by Room</h3>
            <ResponsiveContainer width="100%" height={300}>
              <BarChart data={report.rows} margin={{ top: 0, right: 20, left: -10, bottom: 0 }}>
                <CartesianGrid strokeDasharray="3 3" stroke="var(--c-border)" />
                <XAxis dataKey="room_name" tick={{ fill: "var(--c-text-muted)", fontSize: 12 }} />
                <YAxis tick={{ fill: "var(--c-text-muted)", fontSize: 12 }} />
                <Tooltip
                  contentStyle={{ background: "var(--c-surface)", border: "1px solid var(--c-border)", borderRadius: 8 }}
                  labelStyle={{ color: "var(--c-text)" }}
                />
                <Bar dataKey="total_retrievals" fill="#6366f1" radius={[4, 4, 0, 0]} name="Total Retrievals" />
                <Bar dataKey="overdue_count" fill="#ef4444" radius={[4, 4, 0, 0]} name="Overdue Count" />
              </BarChart>
            </ResponsiveContainer>
          </div>

          {/* Table */}
          <div className="table-wrapper">
            <table>
              <thead>
                <tr>
                  <th>Room</th>
                  <th>Total Retrievals</th>
                  <th>Avg Possession</th>
                  <th>Overdue Count</th>
                  <th>Overdue Rate</th>
                </tr>
              </thead>
              <tbody>
                {report.rows.map(row => (
                  <tr key={row.room_id}>
                    <td style={{ fontWeight: 600 }}>{row.room_name}</td>
                    <td>{row.total_retrievals}</td>
                    <td>{row.avg_possession_minutes.toFixed(0)}m</td>
                    <td style={{ color: row.overdue_count > 0 ? "var(--c-danger)" : "var(--c-text)" }}>
                      {row.overdue_count}
                    </td>
                    <td>
                      {row.total_retrievals > 0
                        ? `${((row.overdue_count / row.total_retrievals) * 100).toFixed(1)}%`
                        : "—"}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </>
      ) : (
        <div style={{ textAlign: "center", padding: "4rem", color: "var(--c-text-muted)" }}>
          No retrieval data yet.
        </div>
      )}
    </div>
  );
}
