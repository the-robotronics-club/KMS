"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { useEffect, useState } from "react";

const navItems = [
  { href: "/admin", label: "Dashboard" },
  { href: "/admin/monitoring", label: "Live Monitoring" },
  { href: "/admin/scripts", label: "Script Runner" },
  { href: "/admin/users", label: "Users" },
  { href: "/admin/permissions", label: "Permissions" },
  { href: "/admin/rooms", label: "Rooms & Keys" },
  { href: "/admin/logs", label: "Audit Logs" },
  { href: "/admin/devices", label: "Device Health" },
  { href: "/admin/reports", label: "Reports" },
];

export default function AdminSidebar() {
  const pathname = usePathname();
  const [open, setOpen] = useState(false);

  useEffect(() => setOpen(false), [pathname]);

  return (
    <>
      <header className="mobile-admin-bar">
        <Link href="/admin" className="sidebar-logo-text"><span>SNTC</span> OPS</Link>
        <button className="menu-toggle" type="button" onClick={() => setOpen(!open)} aria-expanded={open} aria-controls="admin-navigation" aria-label={open ? "Close navigation" : "Open navigation"}>
          <span /><span /><span />
        </button>
      </header>
      {open && <button className="sidebar-scrim" type="button" aria-label="Close navigation" onClick={() => setOpen(false)} />}
      <aside className={`sidebar${open ? " sidebar--open" : ""}`} id="admin-navigation">
      <div className="sidebar-logo">
        <span className="sidebar-mark">S</span>
        <div className="sidebar-logo-text"><span>SNTC</span> <small>OPS CONSOLE</small></div>
      </div>

      <div className="nav-section">Navigation</div>

      {navItems.map(({ href, label }) => (
        <Link
          key={href}
          href={href}
          className={`nav-item${pathname === href ? " active" : ""}`}
          id={`nav-${label.toLowerCase().replace(/\s+/g, "-")}`}
        >
          {label}
        </Link>
      ))}

      <div style={{ flex: 1 }} />

      <div className="nav-section">Account</div>
      <Link href="/keys" className="nav-item">
        Key Portal
      </Link>
      <button
        className="nav-item"
        style={{ color: "var(--c-danger)" }}
        onClick={async () => {
          const { auth } = await import("@/lib/api");
          await auth.logout();
          window.location.href = "/login";
        }}
      >
        Sign Out
      </button>
      </aside>
    </>
  );
}
