"use client";

import { useEffect, useState, useCallback } from "react";
import { useRouter } from "next/navigation";
import { keys, auth, type KeySlot } from "@/lib/api";
import { formatDistanceToNow, format, isPast } from "date-fns";
import Navbar from "@/components/Navbar";

export default function KeysPage() {
  const router = useRouter();
  const [slots, setSlots] = useState<KeySlot[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState("");
  const [proximityVerified, setProximityVerified] = useState(false);
  const [actionLoading, setActionLoading] = useState<string | null>(null);
  const [notification, setNotification] = useState<{ type: "success" | "error"; msg: string } | null>(null);

  const fetchSlots = useCallback(async () => {
    try {
      const data = await keys.list();
      setSlots(data);
    } catch (err: any) {
      if (err.status === 401) {
        router.push("/login");
      } else {
        setError(err.detail || "Failed to load key status.");
      }
    } finally {
      setLoading(false);
    }
  }, [router]);

  useEffect(() => {
    const pv = sessionStorage.getItem("proximity_verified") === "true";
    setProximityVerified(pv);
    fetchSlots();

    // The browser opens /ws/keys against the backend directly — the /api rewrite
    // only covers HTTP and does not upgrade websockets. So this needs the public
    // backend origin, not the frontend's, and the session cookie cannot travel
    // with it: hence the ticket, fetched over /api where the cookie does apply.
    let ws: WebSocket | undefined;
    let closed = false;

    (async () => {
      let ticket: string;
      try {
        ticket = (await auth.wsTicket()).ticket;
      } catch {
        return; // No live updates; the page still polls on every action.
      }
      if (closed) return;

      const apiBase = process.env.NEXT_PUBLIC_API_URL || "";
      const wsOrigin = apiBase
        ? apiBase.replace(/^http/, "ws")
        : `${window.location.protocol === "https:" ? "wss" : "ws"}://${window.location.host}`;
      ws = new WebSocket(`${wsOrigin}/ws/keys?ticket=${encodeURIComponent(ticket)}`);
      ws.onmessage = () => fetchSlots();
      // Live updates are a nicety here; the page already polls on every action and
      // exposes a Refresh button, so a dropped socket degrades rather than breaks.
      ws.onerror = () => {};
    })();

    return () => {
      closed = true;
      ws?.close();
    };
  }, [fetchSlots]);

  async function handleRetrieve(slotId: string) {
    setActionLoading(slotId);
    try {
      await keys.retrieve(slotId);
      setNotification({ type: "success", msg: "Key retrieved successfully. Check your email for details." });
      await fetchSlots();
    } catch (err: any) {
      if (err.error === "not_proximity_verified") {
        router.push("/connect?reason=retrieve");
      } else if (err.error === "slot_unavailable") {
        setNotification({ type: "error", msg: "Another user just retrieved that key. Refresh to see updates." });
      } else {
        setNotification({ type: "error", msg: err.detail || "Failed to retrieve key." });
      }
    } finally {
      setActionLoading(null);
      setTimeout(() => setNotification(null), 5000);
    }
  }

  async function handleReturn(slotId: string) {
    setActionLoading(slotId);
    try {
      await keys.returnKey(slotId);
      setNotification({ type: "success", msg: "Key returned successfully." });
      await fetchSlots();
    } catch (err: any) {
      if (err.error === "not_proximity_verified") {
        router.push("/connect?reason=return");
      } else {
        setNotification({ type: "error", msg: err.detail || "Failed to return key." });
      }
    } finally {
      setActionLoading(null);
      setTimeout(() => setNotification(null), 5000);
    }
  }

  async function handleExtend(slotId: string) {
    setActionLoading(`extend-${slotId}`);
    try {
      const result = await keys.extend(slotId);
      setNotification({ type: "success", msg: `Extended. New due time: ${format(new Date(result.new_due_at), "h:mm a")}` });
      await fetchSlots();
    } catch (err: any) {
      setNotification({ type: "error", msg: err.detail || "Failed to extend." });
    } finally {
      setActionLoading(null);
      setTimeout(() => setNotification(null), 5000);
    }
  }

  return (
    <div style={{ minHeight: "100vh", background: "var(--c-bg)", paddingBottom: "3rem" }}>
      <Navbar />
      <div className="container" style={{ padding: "0 1.5rem" }}>
        {/* Header */}
        <div className="page-header">
          <div>
            <h1 className="page-title">Key Status</h1>
            <p className="page-subtitle">Real-time availability — retrieve or return keys from here</p>
          </div>
          <div style={{ display: "flex", gap: "0.75rem", alignItems: "center" }}>
            {proximityVerified ? (
              <span className="badge badge--available">Proximity Verified</span>
            ) : (
              <a
                href="/connect"
                className="btn btn-ghost btn-sm"
                style={{ borderColor: "var(--c-warning)", color: "var(--c-warning)" }}
              >
                Connect to Enclosure WiFi
              </a>
            )}
            <button onClick={fetchSlots} className="btn btn-ghost btn-sm">Refresh</button>
          </div>
        </div>

        {/* Proximity banner if not verified */}
        {!proximityVerified && (
          <div className="bezel-shell" style={{ marginBottom: "1.5rem" }}>
            <div className="bezel-core" style={{ marginBottom: 0 }}>
              <h3 style={{ fontSize: "0.95rem", fontWeight: 700, color: "var(--c-warning)", marginBottom: "0.25rem" }}>
                Connect to Enclosure WiFi to Retrieve or Return Keys
              </h3>
              <p style={{ fontSize: "0.85rem", color: "var(--c-text-muted)", lineHeight: 1.5 }}>
                Key status is publicly browsable, but key retrieval and returns require physical presence at the SAC A-19 enclosure WiFi network.
              </p>
            </div>
          </div>
        )}

        {/* Notification toast */}
        {notification && (
          <div
            style={{
              padding: "0.875rem 1.25rem",
              borderRadius: "var(--radius)",
              marginBottom: "1.5rem",
              background: notification.type === "success" ? "var(--c-success-glow)" : "var(--c-danger-glow)",
              border: `1px solid ${notification.type === "success" ? "var(--c-success)" : "var(--c-danger)"}`,
              color: notification.type === "success" ? "var(--c-success)" : "var(--c-danger)",
              fontSize: "0.9rem",
              fontWeight: 500,
              animation: "fadeIn 0.3s ease",
            }}
          >
            {notification.msg}
          </div>
        )}

        {/* Key grid */}
        {loading ? (
          <div className="key-grid">
            {[1, 2, 3, 4, 5, 6].map((i) => (
              <div key={i} className="card skeleton" style={{ height: 190 }} />
            ))}
          </div>
        ) : error ? (
          <div style={{ textAlign: "center", padding: "4rem", color: "var(--c-text-muted)" }}>
            {error} —{" "}
            <button onClick={fetchSlots} style={{ color: "var(--c-accent)", background: "none", border: "none", cursor: "pointer" }}>
              retry
            </button>
          </div>
        ) : slots.length === 0 ? (
          <div className="empty-state">
            <h3 className="empty-state-title">No Keys Available</h3>
            <p className="empty-state-desc">
              You don&apos;t have access to any room keys yet. Contact your SAC club coordinator to request room permissions.
            </p>
          </div>
        ) : (
          <div className="key-grid">
            {slots.map((slot) => {
              const isOverdue = slot.due_at && isPast(new Date(slot.due_at));
              return (
                <div key={slot.slot_id} className="bezel-shell">
                  <div
                    className={`bezel-core key-slot-card key-slot-card--${slot.status}`}
                    id={`slot-${slot.slot_id}`}
                  >
                    <div className="slot-number">Slot #{slot.slot_number}</div>
                    <div className="slot-room-name">{slot.room_name || "Unknown Room"}</div>

                    <div style={{ marginBottom: "0.75rem" }}>
                      <span
                        className={`badge badge--${slot.status}${isOverdue ? " badge--overdue" : ""}`}
                      >
                        <span className={`dot dot--${slot.status === "available" ? "green" : slot.status === "retrieved" ? "yellow" : "gray"}`} />
                        {isOverdue ? "OVERDUE" : slot.status.toUpperCase()}
                      </span>
                    </div>

                    {slot.current_holder && (
                      <div className="slot-holder">
                        Holder: {slot.current_holder}
                      </div>
                    )}
                    {slot.due_at && (
                      <div className={`slot-due ${isOverdue ? "text-danger" : ""}`}>
                        {isOverdue ? "Overdue: " : "Due: "}
                        {formatDistanceToNow(new Date(slot.due_at), { addSuffix: true })}
                      </div>
                    )}

                    <div className="slot-actions">
                      {slot.status === "available" && (
                        <button
                          className="btn btn-success btn-sm"
                          disabled={!proximityVerified || actionLoading === slot.slot_id}
                          onClick={() => handleRetrieve(slot.slot_id)}
                          id={`retrieve-${slot.slot_id}`}
                        >
                          {actionLoading === slot.slot_id ? (
                            <span className="spinner" />
                          ) : (
                            "Retrieve Key"
                          )}
                        </button>
                      )}
                      {slot.status === "retrieved" && slot.current_holder && (
                        <>
                          <button
                            className="btn btn-primary btn-sm"
                            disabled={!proximityVerified || actionLoading === slot.slot_id}
                            onClick={() => handleReturn(slot.slot_id)}
                            id={`return-${slot.slot_id}`}
                          >
                            {actionLoading === slot.slot_id ? (
                              <span className="spinner" />
                            ) : (
                              "Return Key"
                            )}
                          </button>
                          <button
                            className="btn btn-ghost btn-sm"
                            disabled={actionLoading === `extend-${slot.slot_id}`}
                            onClick={() => handleExtend(slot.slot_id)}
                            id={`extend-${slot.slot_id}`}
                          >
                            {actionLoading === `extend-${slot.slot_id}` ? (
                              <span className="spinner" />
                            ) : (
                              "+6 Hours"
                            )}
                          </button>
                        </>
                      )}
                      {slot.status === "maintenance" && (
                        <span style={{ fontSize: "0.8rem", color: "var(--c-text-dim)" }}>
                          Under maintenance
                        </span>
                      )}
                    </div>
                  </div>
                </div>
              );
            })}
          </div>
        )}
      </div>
    </div>
  );
}
