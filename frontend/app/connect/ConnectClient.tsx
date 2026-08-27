"use client";

import { useCallback, useEffect, useState } from "react";
import { useRouter } from "next/navigation";
import { proximity } from "@/lib/api";

interface ConnectPageClientProps {
  deviceId: string;
  code: string;
}

export default function ConnectPageClient({ deviceId, code }: ConnectPageClientProps) {
  const router = useRouter();
  // "idle" = waiting for the user to type a code from the enclosure display.
  const [status, setStatus] = useState<"idle" | "verifying" | "success" | "error">(
    code ? "verifying" : "idle"
  );
  const [entered, setEntered] = useState(code);
  const [error, setError] = useState("");

  const verify = useCallback(
    async (value: string) => {
      setStatus("verifying");
      setError("");
      try {
        // device_id is optional — the code alone resolves to a device server-side.
        const result = await proximity.verify(value.trim(), deviceId || undefined);
        if (result.proximity_verified) {
          setStatus("success");
          sessionStorage.setItem("proximity_verified", "true");
          if (deviceId) sessionStorage.setItem("proximity_device_id", deviceId);
          setTimeout(() => router.push("/keys"), 1500);
        }
      } catch (err: any) {
        setStatus("error");
        setError(
          err.status === 401
            ? "Sign in first, then enter the code."
            : err.error || err.detail || "Code invalid or expired. Get a fresh one from the enclosure."
        );
      }
    },
    [deviceId, router]
  );

  // Captive-portal path: the code arrived in the URL, so skip the form.
  useEffect(() => {
    if (code) verify(code);
  }, [code, verify]);

  function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    verify(entered);
  }

  return (
    <div className="auth-container">
      <div className="auth-card animate-fade-in" style={{ textAlign: "center" }}>
        {(status === "idle" || status === "error") && (
          <>
            <div style={{ fontSize: "3rem", marginBottom: "1rem" }}>
              {status === "error" ? "⚠️" : "📡"}
            </div>
            <h1 className="auth-title">Prove You&apos;re at the Enclosure</h1>
            <p className="auth-subtitle" style={{ marginTop: "0.5rem" }}>
              Enter the code shown after joining the enclosure WiFi.
            </p>

            {error && (
              <p style={{ color: "var(--c-warning)", margin: "1rem 0 0", fontSize: "0.9rem" }}>
                {error}
              </p>
            )}

            <form onSubmit={handleSubmit} style={{ marginTop: "1.5rem", textAlign: "left" }}>
              <div className="form-group">
                <label htmlFor="proximity-code" className="label">Proximity Code</label>
                <input
                  id="proximity-code"
                  className="input"
                  value={entered}
                  onChange={(e) => setEntered(e.target.value)}
                  placeholder="e.g. 4F2A91"
                  autoComplete="off"
                  autoCapitalize="characters"
                  autoFocus
                  required
                  style={{ letterSpacing: "0.25em", textAlign: "center", fontSize: "1.1rem" }}
                />
              </div>
              <button type="submit" className="btn btn-primary btn-lg btn-full">
                Verify
              </button>
            </form>

            <div className="proximity-banner" style={{ marginTop: "1.5rem" }}>
              <span className="proximity-banner-icon">📶</span>
              <div className="proximity-banner-text">
                <h3>How to get a code</h3>
                <p>
                  Join the WiFi network at the SAC A-19 enclosure. A page opens with a
                  short code. Rejoin your normal network, then enter it here.
                </p>
              </div>
            </div>
          </>
        )}

        {status === "verifying" && (
          <>
            <div style={{ fontSize: "3rem", marginBottom: "1rem" }}>📡</div>
            <h1 className="auth-title">Connecting to Enclosure</h1>
            <p className="auth-subtitle" style={{ marginTop: "0.5rem" }}>
              Verifying proximity code...
            </p>
            <div style={{ display: "flex", justifyContent: "center", marginTop: "2rem" }}>
              <div style={{ width: 40, height: 40, border: "3px solid var(--c-border)", borderTopColor: "var(--c-primary)", borderRadius: "50%", animation: "spin 0.8s linear infinite" }} />
            </div>
          </>
        )}

        {status === "success" && (
          <>
            <div style={{ fontSize: "3rem", marginBottom: "1rem", animation: "fadeIn 0.5s ease" }}>✅</div>
            <h1 className="auth-title" style={{ color: "var(--c-success)" }}>Proximity Verified!</h1>
            <p className="auth-subtitle" style={{ marginTop: "0.5rem" }}>
              You are at the enclosure. Redirecting to key management...
            </p>
            <div
              style={{
                marginTop: "1.5rem",
                padding: "0.75rem 1.25rem",
                background: "var(--c-success-glow)",
                border: "1px solid var(--c-success)",
                borderRadius: "var(--radius-sm)",
                fontSize: "0.85rem",
                color: "var(--c-success)",
              }}
            >
              ⏱️ Proximity flag valid for 5 minutes
            </div>
          </>
        )}
      </div>
    </div>
  );
}
