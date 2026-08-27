import type { Metadata } from "next";
import AdminSidebar from "@/components/admin/AdminSidebar";

export const metadata: Metadata = {
  title: "Admin Panel — SNTC",
  description: "SNTC administration panel for user, permission, and device management.",
};

export default function AdminLayout({ children }: { children: React.ReactNode }) {
  return (
    <div className="layout-with-sidebar">
      <AdminSidebar />
      <main className="main-content">{children}</main>
    </div>
  );
}
