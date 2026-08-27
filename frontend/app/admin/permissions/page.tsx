"use client";

import { useEffect, useState } from "react";
import { admin, type Room, type User } from "@/lib/api";

export default function AdminPermissionsPage() {
  const [rooms, setRooms] = useState<Room[]>([]);
  const [users, setUsers] = useState<User[]>([]);
  const [loading, setLoading] = useState(true);
  const [form, setForm] = useState({ user_id: "", room_id: "", expires_at: "" });
  const [success, setSuccess] = useState("");
  const [error, setError] = useState("");

  useEffect(() => {
    Promise.all([admin.listRooms(), admin.listUsers()])
      .then(([r, u]) => { setRooms(r); setUsers(u); })
      .finally(() => setLoading(false));
  }, []);

  async function handleGrant(e: React.FormEvent) {
    e.preventDefault();
    setError(""); setSuccess("");
    try {
      await admin.grantPermission(form.user_id, form.room_id, form.expires_at || undefined);
      setSuccess(`✅ Permission granted.`);
      setForm({ user_id: "", room_id: "", expires_at: "" });
    } catch (err: any) {
      setError(err.detail || "Failed to grant permission.");
    }
  }

  return (
    <div>
      <div className="page-header">
        <div>
          <h1 className="page-title">🔐 Permission Management</h1>
          <p className="page-subtitle">Grant or revoke room access for users</p>
        </div>
      </div>

      {error && <div style={{ color: "var(--c-danger)", marginBottom: "1rem", padding: "0.75rem", background: "var(--c-danger-glow)", border: "1px solid var(--c-danger)", borderRadius: "var(--radius-sm)" }}>{error}</div>}
      {success && <div style={{ color: "var(--c-success)", marginBottom: "1rem", padding: "0.75rem", background: "var(--c-success-glow)", border: "1px solid var(--c-success)", borderRadius: "var(--radius-sm)" }}>{success}</div>}

      <div className="card" style={{ marginBottom: "2rem" }}>
        <h3 style={{ marginBottom: "1.25rem" }}>Grant Room Access</h3>
        {loading ? (
          <p style={{ color: "var(--c-text-muted)" }}>Loading users and rooms...</p>
        ) : (
          <form onSubmit={handleGrant} style={{ display: "grid", gridTemplateColumns: "1fr 1fr 1fr auto", gap: "1rem", alignItems: "flex-end" }}>
            <div className="form-group">
              <label className="label">User *</label>
              <select className="input" value={form.user_id} onChange={e => setForm({ ...form, user_id: e.target.value })} required>
                <option value="">Select user...</option>
                {users.filter(u => u.is_active).map(u => (
                  <option key={u.id} value={u.id}>{u.name} ({u.email})</option>
                ))}
              </select>
            </div>
            <div className="form-group">
              <label className="label">Room *</label>
              <select className="input" value={form.room_id} onChange={e => setForm({ ...form, room_id: e.target.value })} required>
                <option value="">Select room...</option>
                {rooms.map(r => (
                  <option key={r.id} value={r.id}>{r.name}{r.block ? ` (${r.block})` : ""}</option>
                ))}
              </select>
            </div>
            <div className="form-group">
              <label className="label">Expires At (optional)</label>
              <input className="input" type="datetime-local" value={form.expires_at} onChange={e => setForm({ ...form, expires_at: e.target.value })} />
            </div>
            <button type="submit" className="btn btn-primary">Grant Access</button>
          </form>
        )}
      </div>

      <div className="card">
        <h3 style={{ marginBottom: "1rem" }}>Access Matrix</h3>
        {!loading && (
          <div className="table-wrapper">
            <table>
              <thead>
                <tr>
                  <th>User</th>
                  {rooms.map(r => <th key={r.id}>{r.name}</th>)}
                </tr>
              </thead>
              <tbody>
                {users.filter(u => u.is_active).map(u => (
                  <tr key={u.id}>
                    <td style={{ fontWeight: 500 }}>
                      {u.name}
                      <span className={`badge badge--${u.role}`} style={{ marginLeft: "0.5rem", fontSize: "0.65rem" }}>{u.role}</span>
                    </td>
                    {rooms.map(r => (
                      <td key={r.id} style={{ textAlign: "center" }}>
                        {/* In a real implementation, fetch permissions and show checkmarks */}
                        <span style={{ color: "var(--c-text-dim)" }}>—</span>
                      </td>
                    ))}
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
    </div>
  );
}
