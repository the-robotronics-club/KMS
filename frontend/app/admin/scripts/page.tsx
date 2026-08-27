"use client";

import { useEffect, useState } from "react";
import { scripts, type ScriptExecution, type ScriptStatus } from "@/lib/api";
import { AbortButton, ScriptRunner } from "@/components/AbortButton";
import { formatDistanceToNow } from "date-fns";

const SCRIPTS = [
  { name: "sync_keys", label: "Sync Keys", description: "Synchronize key statuses with all devices" },
  { name: "backup_db", label: "Backup Database", description: "Create a full database backup" },
  { name: "generate_report", label: "Generate Report", description: "Generate usage statistics report" },
  { name: "cleanup_logs", label: "Cleanup Logs", description: "Remove log files older than 30 days" },
] as const;

const statusColors: Record<ScriptStatus, string> = {
  running: "var(--c-info)",
  completed: "var(--c-success)",
  killed: "var(--c-warning)",
  failed: "var(--c-danger)",
};

const statusIcons: Record<ScriptStatus, string> = {
  running: "⏳",
  completed: "✅",
  killed: "⏹️",
  failed: "❌",
};

function getStatusBadge(status: ScriptStatus) {
  return (
    <span
      className="badge"
      style={{
        background: `${statusColors[status]}20`,
        color: statusColors[status],
        border: `1px solid ${statusColors[status]}`,
        textTransform: "capitalize",
      }}
    >
      {statusIcons[status]} {status}
    </span>
  );
}

export default function AdminScriptsPage() {
  const [executions, setExecutions] = useState<ScriptExecution[]>([]);
  const [loading, setLoading] = useState(true);
  const [selectedExecution, setSelectedExecution] = useState<ScriptExecution | null>(null);
  const [showOutput, setShowOutput] = useState<string | null>(null);

  useEffect(() => {
    fetchExecutions();
    const interval = setInterval(fetchExecutions, 10000);
    return () => clearInterval(interval);
  }, []);

  async function fetchExecutions() {
    try {
      const data = await scripts.list();
      setExecutions(data);
    } catch (e) {
      console.error("Failed to fetch executions:", e);
    } finally {
      setLoading(false);
    }
  }

  return (
    <div>
      <div className="page-header">
        <div>
          <h1 className="page-title">📜 Script Runner</h1>
          <p className="page-subtitle">Execute maintenance scripts and monitor their progress in real-time</p>
        </div>
      </div>

      {/* Available Scripts */}
      <section style={{ marginBottom: "2rem" }}>
        <h2 style={{ fontSize: "1.1rem", marginBottom: "1rem", color: "var(--c-text)" }}>Available Scripts</h2>
        <div style={{ display: "grid", gap: "0.75rem", gridTemplateColumns: "repeat(auto-fit, minmax(280px, 1fr))" }}>
          {SCRIPTS.map((script) => (
            <ScriptRunner key={script.name} scriptName={script.name} label={script.label}>
              {({ isRunning, status, output, error, run, abort, reset }) => (
                <div className="card" style={{ padding: "1.25rem" }}>
                  <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start", marginBottom: "0.75rem" }}>
                    <div>
                      <div style={{ fontWeight: 600, color: "var(--c-text)" }}>{script.label}</div>
                      <div style={{ fontSize: "0.8rem", color: "var(--c-text-muted)", marginTop: "0.25rem" }}>
                        {script.description}
                      </div>
                    </div>
                    {status && getStatusBadge(status as ScriptStatus)}
                  </div>

                  <div style={{ display: "flex", gap: "0.5rem", flexWrap: "wrap", alignItems: "center" }}>
                    <button
                      onClick={run}
                      disabled={isRunning}
                      className={isRunning ? "btn btn-ghost" : "btn btn-primary"}
                      style={{ padding: "0.5rem 1rem", fontSize: "0.875rem" }}
                    >
                      {isRunning ? "Running..." : "Run"}
                    </button>

                    <AbortButton execution_id={null} isRunning={isRunning} onKill={abort} />

                    {error && (
                      <span style={{ color: "var(--c-danger)", fontSize: "0.8rem" }}>Error: {error.message}</span>
                    )}
                  </div>

                  {output && (
                    <div style={{ marginTop: "0.75rem" }}>
                      <button
                        onClick={() => setShowOutput(output)}
                        style={{
                          background: "none",
                          border: "none",
                          color: "var(--c-primary)",
                          cursor: "pointer",
                          fontSize: "0.8rem",
                          padding: 0,
                          textAlign: "left",
                        }}
                      >
                        Show output ({output.length} chars)
                      </button>
                    </div>
                  )}
                </div>
              )}
            </ScriptRunner>
          ))}
        </div>
      </section>

      {/* Execution History */}
      <section>
        <h2 style={{ fontSize: "1.1rem", marginBottom: "1rem", color: "var(--c-text)" }}>Execution History</h2>
        {loading ? (
          <div style={{ textAlign: "center", padding: "3rem" }}>Loading...</div>
        ) : executions.length === 0 ? (
          <div className="card" style={{ textAlign: "center", padding: "3rem" }}>
            <div style={{ fontSize: "2.5rem", marginBottom: "1rem" }}>📜</div>
            <p style={{ color: "var(--c-text-muted)" }}>No script executions yet.</p>
          </div>
        ) : (
          <div className="table-wrapper">
            <table>
              <thead>
                <tr>
                  <th>Script</th>
                  <th>Status</th>
                  <th>Started</th>
                  <th>Finished</th>
                  <th>Duration</th>
                  <th>Exit Code</th>
                  <th>Actions</th>
                </tr>
              </thead>
              <tbody>
                {executions.map((exec) => (
                  <tr key={exec.id}>
                    <td style={{ fontWeight: 600 }}>{exec.script_name}</td>
                    <td>{getStatusBadge(exec.status as ScriptStatus)}</td>
                    <td className="font-mono" style={{ fontSize: "0.8rem" }}>
                      {formatDistanceToNow(new Date(exec.started_at), { addSuffix: true })}
                    </td>
                    <td className="font-mono" style={{ fontSize: "0.8rem" }}>
                      {exec.finished_at
                        ? formatDistanceToNow(new Date(exec.finished_at), { addSuffix: true })
                        : "—"}
                    </td>
                    <td className="font-mono" style={{ fontSize: "0.8rem" }}>
                      {exec.finished_at
                        ? Math.round((new Date(exec.finished_at).getTime() - new Date(exec.started_at).getTime()) / 1000) + "s"
                        : "—"}
                    </td>
                    <td className="font-mono" style={{ fontSize: "0.8rem" }}>
                      {exec.exit_code !== null ? exec.exit_code : "—"}
                    </td>
                    <td>
                      <button
                        onClick={() => {
                          setSelectedExecution(exec);
                          setShowOutput(exec.output ?? exec.error ?? "No output");
                        }}
                        className="btn btn-ghost btn-sm"
                        style={{ padding: "0.25rem 0.75rem", fontSize: "0.8rem" }}
                      >
                        View Output
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </section>

      {/* Output Modal */}
      {showOutput && (
        <div
          className="modal-overlay"
          onClick={() => setShowOutput(null)}
          style={{
            position: "fixed",
            inset: 0,
            background: "rgba(0,0,0,0.5)",
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            zIndex: 1000,
            padding: "1rem",
          }}
        >
          <div
            className="card"
            onClick={(e) => e.stopPropagation()}
            style={{
              maxWidth: "800px",
              width: "100%",
              maxHeight: "70vh",
              display: "flex",
              flexDirection: "column",
            }}
          >
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", padding: "1rem", borderBottom: "1px solid var(--c-border)" }}>
              <h3 style={{ margin: 0 }}>{selectedExecution?.script_name} Output</h3>
              <button
                onClick={() => setShowOutput(null)}
                style={{ background: "none", border: "none", fontSize: "1.5rem", cursor: "pointer", color: "var(--c-text-muted)" }}
              >
                ×
              </button>
            </div>
            <div style={{ flex: 1, overflow: "auto", padding: "1rem" }}>
              <pre
                style={{
                  fontFamily: "monospace",
                  fontSize: "0.75rem",
                  lineHeight: 1.5,
                  whiteSpace: "pre-wrap",
                  wordBreak: "break-word",
                  color: "var(--c-text)",
                  margin: 0,
                }}
              >
                {showOutput}
              </pre>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}