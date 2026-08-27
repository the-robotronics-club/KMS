"use client";

import { useState, useRef, useEffect } from "react";
import { useRouter } from "next/navigation";
import { auth } from "@/lib/api";

export default function TOTPVerifyPage() {
  const router = useRouter();
  const [code, setCode] = useState("");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    inputRef.current?.focus();
  }, []);

  async function handleSubmit(e: React.FormEvent | { preventDefault: () => void }, overrideCode?: string) {
    e.preventDefault();
    // The 6th digit auto-submits, so a Verify click lands a second request.
    // temp_token is single-use, so that one always fails as "expired".
    if (loading) return;
    setError("");
    setLoading(true);
    const finalCode = (overrideCode ?? code).trim();
    try {
      await auth.verifyTOTP(finalCode);
      router.replace("/keys");
    } catch (err: any) {
      const detail = err.detail || "Invalid code. Try again.";
      // A consumed/expired token can never succeed here — bounce to login
      // rather than leaving the user retrying a dead token forever.
      if (typeof detail === "string" && detail.includes("token")) {
        router.push("/login");
        return;
      }
      setError(detail);
      setCode("");
      inputRef.current?.focus();
    } finally {
      setLoading(false);
    }
  }

  // Auto-submit when 6 digits entered
  function handleCodeChange(val: string) {
    const digits = val.replace(/\D/g, "").slice(0, 6);
    setCode(digits);
    if (digits.length === 6) {
      // Pass the digits explicitly — `code` in the closure is the stale
      // pre-update value (empty on first call), which would submit "".
      handleSubmit({ preventDefault: () => {} } as any, digits);
    }
  }

  return (
    <div className="auth-container">
      <div className="auth-card animate-fade-in">
        <div className="auth-logo">
          <div className="auth-logo-icon">📱</div>
          <h1 className="auth-title">Enter TOTP Code</h1>
          <p className="auth-subtitle">Open your authenticator app and enter the 6-digit code</p>
        </div>

        {error && (
          <div
            style={{
              background: "var(--c-danger-glow)",
              border: "1px solid var(--c-danger)",
              borderRadius: "var(--radius-sm)",
              padding: "0.75rem 1rem",
              fontSize: "0.875rem",
              color: "var(--c-danger)",
              marginBottom: "1rem",
              textAlign: "center",
            }}
          >
            ⚠️ {error}
          </div>
        )}

        <form className="auth-form" onSubmit={handleSubmit} id="totp-form">
          <div className="form-group">
            <label htmlFor="totp-code" className="label" style={{ textAlign: "center" }}>
              6-Digit Code
            </label>
            <input
              id="totp-code"
              ref={inputRef}
              type="text"
              inputMode="numeric"
              pattern="[0-9]*"
              className="input input--code"
              placeholder="000 000"
              value={code.length > 3 ? `${code.slice(0, 3)} ${code.slice(3)}` : code}
              onChange={(e) => handleCodeChange(e.target.value.replace(/\s/g, ""))}
              maxLength={7}
              autoComplete="one-time-code"
            />
          </div>

          <button
            type="submit"
            id="totp-submit"
            className="btn btn-primary btn-lg btn-full"
            disabled={loading || code.length < 6}
          >
            {loading ? <><span className="spinner" /> Verifying...</> : "Verify →"}
          </button>
        </form>

        <p style={{ textAlign: "center", fontSize: "0.8rem", color: "var(--c-text-dim)", marginTop: "1rem" }}>
          Code refreshes every 30 seconds. Accepting ±1 window.
        </p>
      </div>
    </div>
  );
}
