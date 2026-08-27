"use client";

import Link from "next/link";
import { usePathname, useRouter } from "next/navigation";
import { useEffect, useState } from "react";
import { useTheme } from "@/components/ThemeProvider";
import { auth } from "@/lib/api";

export default function Navbar() {
  const pathname = usePathname();
  const router = useRouter();
  const { theme, toggleTheme } = useTheme();
  const [open, setOpen] = useState(false);
  const [user, setUser] = useState<Awaited<ReturnType<typeof auth.me>> | null>(null);
  const [loggingOut, setLoggingOut] = useState(false);

  useEffect(() => setOpen(false), [pathname]);

  useEffect(() => {
    auth.me().then(setUser).catch(() => setUser(null));
  }, [pathname]);

  async function handleLogout() {
    if (loggingOut) return;
    setLoggingOut(true);
    try {
      await auth.logout();
    } finally {
      setUser(null);
      setOpen(false);
      setLoggingOut(false);
      router.replace("/login");
    }
  }

  return (
    <header className="navbar-wrapper">
      <div className="navbar-pill">
        <Link href="/" className="navbar-brand">
          <span>SnTC</span>
          <span className="navbar-brand-sub">Key System</span>
        </Link>
        {user && (
          <div className="navbar-user-summary" aria-label="Logged-in user details">
            <span className="navbar-user-status"><span className="dot dot--green" /> You are logged in</span>
            <strong>{user.name}</strong>
            <span>Roll No: {user.roll_no || "—"}</span>
            <span>Access: {user.role === "admin" ? "Master" : user.access_names.join(", ") || "None"}</span>
          </div>
        )}
        <button className="navbar-menu-toggle" type="button" onClick={() => setOpen(!open)} aria-expanded={open} aria-controls="site-navigation" aria-label={open ? "Close menu" : "Open menu"}>
          <span /><span /><span />
        </button>
        <nav className={`navbar-links${open ? " navbar-links--open" : ""}`} id="site-navigation">
          <Link
            href="/"
            className={`navbar-link${pathname === "/" ? " active" : ""}`}
          >
            Home
          </Link>
          <Link
            href="/keys"
            className={`navbar-link${pathname === "/keys" ? " active" : ""}`}
          >
            Key Status
          </Link>
          {user ? (
            <button type="button" className="navbar-link navbar-link--button" onClick={handleLogout} disabled={loggingOut}>
              {loggingOut ? "Signing Out" : "Sign Out"}
            </button>
          ) : (
            <Link
              href="/login"
              className={`navbar-link${pathname === "/login" || pathname?.startsWith("/login") ? " active" : ""}`}
            >
              Sign In
            </Link>
          )}
          <button
            onClick={toggleTheme}
            className="theme-toggle-btn"
            aria-label="Toggle Theme"
          >
            {theme === "dark" ? "Light Mode" : "Dark Mode"}
          </button>
        </nav>
      </div>
    </header>
  );
}
