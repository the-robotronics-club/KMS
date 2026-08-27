"use client";

import { useState } from "react";
import { useRouter } from "next/navigation";
import Link from "next/link";
import { auth } from "@/lib/api";
import Navbar from "@/components/Navbar";

export default function LoginPage() {
  const router = useRouter();
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    setError("");
    setLoading(true);
    try {
      const result = await auth.login(email, password);
      if (result.requires_totp_setup) {
        router.push("/totp/setup");
      } else if (result.requires_totp) {
        router.push("/login/totp");
      } else {
        setError("Unexpected response from the server. Please try again.");
      }
    } catch (err: any) {
      setError(err.detail || "Invalid email or password.");
    } finally {
      setLoading(false);
    }
  }

  // Off unless NEXT_PUBLIC_DEMO_MODE is set at build time, so a production
  // bundle carries no seed credentials at all.
  const demoEmail = process.env.NEXT_PUBLIC_DEMO_EMAIL;
  const demoPassword = process.env.NEXT_PUBLIC_DEMO_PASSWORD;
  const demoEnabled =
    process.env.NEXT_PUBLIC_DEMO_MODE === "true" && !!demoEmail && !!demoPassword;

  function handleDemoFill() {
    setEmail(demoEmail!);
    setPassword(demoPassword!);
  }

  return (
    <div style={{ minHeight: "100vh", background: "var(--c-bg)" }}>
      <Navbar />
      <div className="auth-container" style={{ minHeight: "calc(100vh - 120px)", paddingTop: "0" }}>
        <div className="auth-card animate-fade-in">
          <div className="auth-logo">
            <h1 className="auth-title">Sign In</h1>
            <p className="auth-subtitle">IIT Mandi SAC — Key Management</p>
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
              }}
            >
              {error}
            </div>
          )}

          <form className="auth-form" onSubmit={handleSubmit} id="login-form">
            <div className="form-group">
              <label htmlFor="email" className="label">Email Address</label>
              <input
                id="email"
                type="email"
                className="input"
                placeholder="you@iitmandi.ac.in"
                value={email}
                onChange={(e) => setEmail(e.target.value)}
                required
                autoComplete="email"
                autoFocus
              />
            </div>
            <div className="form-group">
              <label htmlFor="password" className="label">Password</label>
              <input
                id="password"
                type="password"
                className="input"
                placeholder="••••••••"
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                required
                autoComplete="current-password"
              />
            </div>

            <button
              type="submit"
              id="login-submit"
              className="btn btn-primary btn-lg btn-full"
              disabled={loading}
            >
              {loading ? <><span className="spinner" /> Signing in...</> : "Continue"}
            </button>
          </form>

          {demoEnabled && (
            <div style={{ marginTop: "1rem" }}>
              <button
                type="button"
                onClick={handleDemoFill}
                className="btn btn-ghost btn-sm btn-full"
                style={{ fontSize: "0.8rem" }}
              >
                Fill Demo Account Credentials
              </button>
            </div>
          )}

          <div style={{ marginTop: "1.5rem", textAlign: "center" }}>
            <Link href="/keys" style={{ fontSize: "0.8rem", color: "var(--c-text-dim)" }}>
              Check key availability without signing in →
            </Link>
          </div>
        </div>
      </div>
    </div>
  );
}
