"use client";

import { useEffect, useState } from "react";
import { useRouter } from "next/navigation";
import { auth } from "@/lib/api";
import { QRCodeSVG } from "qrcode.react";

export default function TOTPSetupPage() {
  const router = useRouter();
  const [data, setData] = useState<{ totp_uri: string; secret: string } | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState("");
  const [copied, setCopied] = useState(false);

  useEffect(() => {
    auth.setupTOTP()
      .then(setData)
      .catch((err: any) => setError(err.detail || "Failed to generate TOTP. Please re-authenticate."))
      .finally(() => setLoading(false));
  }, [router]);

  function copySecret() {
    if (data?.secret) {
      navigator.clipboard.writeText(data.secret);
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    }
  }

  return (
    <div className="auth-container">
      <div className="auth-card animate-fade-in" style={{ maxWidth: 500 }}>
        <div className="auth-logo">
          <div className="auth-logo-icon">🛡️</div>
          <h1 className="auth-title">Set Up Two-Factor Auth</h1>
          <p className="auth-subtitle">Scan the QR code with Google Authenticator, Authy, or any TOTP app</p>
        </div>

        {loading && (
          <div style={{ display: "flex", justifyContent: "center", padding: "2rem" }}>
            <div className="spinner" style={{ width: 36, height: 36, borderWidth: 3 }} />
          </div>
        )}

        {error && (
          <div style={{ color: "var(--c-danger)", textAlign: "center", padding: "1rem" }}>{error}</div>
        )}

        {data && (
          <>
            <div
              style={{
                background: "white",
                padding: "1.25rem",
                borderRadius: "var(--radius)",
                display: "flex",
                justifyContent: "center",
                margin: "0 auto 1.5rem",
                width: "fit-content",
              }}
            >
              <QRCodeSVG value={data.totp_uri} size={200} level="M" />
            </div>

            <div style={{ marginBottom: "1.5rem" }}>
              <p className="label" style={{ textAlign: "center", marginBottom: "0.5rem" }}>
                Or enter manually:
              </p>
              <div
                style={{
                  display: "flex",
                  alignItems: "center",
                  gap: "0.5rem",
                  background: "var(--c-bg3)",
                  border: "1px solid var(--c-border)",
                  borderRadius: "var(--radius-sm)",
                  padding: "0.625rem 1rem",
                }}
              >
                <code style={{ flex: 1, fontSize: "0.85rem", letterSpacing: "0.1em", color: "var(--c-accent)", wordBreak: "break-all" }}>
                  {data.secret}
                </code>
                <button
                  onClick={copySecret}
                  className="btn btn-ghost btn-sm"
                  style={{ flexShrink: 0 }}
                >
                  {copied ? "✓ Copied" : "Copy"}
                </button>
              </div>
            </div>

            <div
              style={{
                background: "var(--c-warning-glow)",
                border: "1px solid var(--c-warning)",
                borderRadius: "var(--radius-sm)",
                padding: "0.875rem 1rem",
                fontSize: "0.8rem",
                color: "var(--c-warning)",
                marginBottom: "1.5rem",
              }}
            >
              ⚠️ Save this secret somewhere safe. It will not be shown again. If you lose access to your authenticator, an admin will need to re-enroll you.
            </div>

            <a href="/login/totp" className="btn btn-primary btn-lg btn-full">
              {"I've Added It"} &rarr; Continue
            </a>
          </>
        )}
      </div>
    </div>
  );
}
