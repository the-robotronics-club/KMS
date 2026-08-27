"use client";

import { useEffect, useState } from "react";
import { admin, type User } from "@/lib/api";

export default function AdminUsersPage() {
  const [users, setUsers] = useState<User[]>([]);
  const [loading, setLoading] = useState(true);
  const [showCreate, setShowCreate] = useState(false);
  const [form, setForm] = useState({ name: "", email: "", roll_no: "", role: "member", password: "" });
  const [error, setError] = useState("");
  const [success, setSuccess] = useState("");
  const [importing, setImporting] = useState(false);

  useEffect(() => { fetchUsers(); }, []);

  async function fetchUsers() {
    setLoading(true);
    try { setUsers(await admin.listUsers()); } finally { setLoading(false); }
  }

  async function handleCreate(e: React.FormEvent) {
    e.preventDefault();
    setError(""); setSuccess("");
    try {
      await admin.createUser({ ...form });
      setSuccess(`✅ User ${form.email} created.`);
      setShowCreate(false);
      setForm({ name: "", email: "", roll_no: "", role: "member", password: "" });
      fetchUsers();
    } catch (err: any) { setError(err.detail || "Failed to create user."); }
  }

  async function handleDeactivate(userId: string, isActive: boolean) {
    try {
      await admin.updateUser(userId, { is_active: !isActive });
      fetchUsers();
    } catch {}
  }

  async function handleImport(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    setImporting(true);
    try {
      const result = await admin.bulkImport(file);
      setSuccess(`✅ Imported ${result.created} users. ${result.errors?.length || 0} errors.`);
      fetchUsers();
    } catch { setError("Import failed."); } finally { setImporting(false); }
  }

  return (
    <div>
      <div className="page-header">
        <div>
          <h1 className="page-title">👥 User Management</h1>
          <p className="page-subtitle">{users.length} registered users</p>
        </div>
        <div style={{ display: "flex", gap: "0.75rem" }}>
          <label className={`btn btn-ghost btn-sm${importing ? " disabled" : ""}`} style={{ cursor: "pointer" }}>
            {importing ? "Importing..." : "📤 CSV Import"}
            <input type="file" accept=".csv" style={{ display: "none" }} onChange={handleImport} disabled={importing} />
          </label>
          <button className="btn btn-primary btn-sm" onClick={() => setShowCreate(true)}>+ Add User</button>
        </div>
      </div>

      {error && <div style={{ color: "var(--c-danger)", marginBottom: "1rem", padding: "0.75rem", background: "var(--c-danger-glow)", border: "1px solid var(--c-danger)", borderRadius: "var(--radius-sm)" }}>{error}</div>}
      {success && <div style={{ color: "var(--c-success)", marginBottom: "1rem", padding: "0.75rem", background: "var(--c-success-glow)", border: "1px solid var(--c-success)", borderRadius: "var(--radius-sm)" }}>{success}</div>}

      {showCreate && (
        <div className="card" style={{ marginBottom: "1.5rem" }}>
          <h3 style={{ marginBottom: "1rem" }}>Create New User</h3>
          <form onSubmit={handleCreate} style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "1rem" }}>
            <div className="form-group">
              <label className="label">Full Name *</label>
              <input className="input" value={form.name} onChange={e => setForm({ ...form, name: e.target.value })} required />
            </div>
            <div className="form-group">
              <label className="label">Email *</label>
              <input className="input" type="email" value={form.email} onChange={e => setForm({ ...form, email: e.target.value })} required />
            </div>
            <div className="form-group">
              <label className="label">Roll Number</label>
              <input className="input" value={form.roll_no} onChange={e => setForm({ ...form, roll_no: e.target.value })} />
            </div>
            <div className="form-group">
              <label className="label">Role</label>
              <select className="input" value={form.role} onChange={e => setForm({ ...form, role: e.target.value })}>
                <option value="member">Member</option>
                <option value="coordinator">Coordinator</option>
                <option value="admin">Admin</option>
              </select>
            </div>
            <div className="form-group" style={{ gridColumn: "1 / -1" }}>
              <label className="label">Initial Password *</label>
              <input className="input" type="password" value={form.password} onChange={e => setForm({ ...form, password: e.target.value })} required minLength={8} />
            </div>
            <div style={{ gridColumn: "1 / -1", display: "flex", gap: "0.75rem" }}>
              <button type="submit" className="btn btn-primary">Create User</button>
              <button type="button" className="btn btn-ghost" onClick={() => setShowCreate(false)}>Cancel</button>
            </div>
          </form>
        </div>
      )}

      <div className="table-wrapper">
        <table>
          <thead>
            <tr>
              <th>Name</th>
              <th>Email</th>
              <th>Roll No</th>
              <th>Role</th>
              <th>TOTP</th>
              <th>Status</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody>
            {loading ? (
              <tr><td colSpan={7} style={{ textAlign: "center", padding: "2rem", color: "var(--c-text-muted)" }}>Loading...</td></tr>
            ) : users.map(user => (
              <tr key={user.id} id={`user-${user.id}`}>
                <td style={{ fontWeight: 600 }}>{user.name}</td>
                <td style={{ color: "var(--c-text-muted)", fontSize: "0.85rem" }}>{user.email}</td>
                <td style={{ fontFamily: "var(--font-mono, monospace)", fontSize: "0.8rem" }}>{user.roll_no || "—"}</td>
                <td><span className={`badge badge--${user.role}`}>{user.role}</span></td>
                <td>
                  {user.totp_enrolled_at ? (
                    <span className="badge badge--available">✓ Enrolled</span>
                  ) : (
                    <span className="badge badge--maintenance">Not set</span>
                  )}
                </td>
                <td>
                  <span className={`badge badge--${user.is_active ? "available" : "maintenance"}`}>
                    {user.is_active ? "Active" : "Inactive"}
                  </span>
                </td>
                <td>
                  <div style={{ display: "flex", gap: "0.5rem" }}>
                    <button
                      className={`btn btn-sm btn-${user.is_active ? "danger" : "success"}`}
                      onClick={() => handleDeactivate(user.id, user.is_active)}
                    >
                      {user.is_active ? "Deactivate" : "Activate"}
                    </button>
                    <button
                      className="btn btn-sm btn-ghost"
                      onClick={async () => {
                        if (confirm(`Reset TOTP for ${user.name}?`)) {
                          await admin.reenrollTOTP(user.id);
                          alert("TOTP reset. User must re-enroll on next login.");
                        }
                      }}
                    >
                      🔄 TOTP
                    </button>
                  </div>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <div style={{ marginTop: "0.75rem", fontSize: "0.8rem", color: "var(--c-text-dim)" }}>
        CSV format: name, email, roll_no, role (member/coordinator/admin), password
      </div>
    </div>
  );
}
