import { Suspense } from "react";
import ConnectClient from "./ConnectClient";

export default async function ConnectPage({
  searchParams,
}: {
  searchParams: Promise<{ device_id?: string; code?: string }>;
}) {
  const params = await searchParams;
  const deviceId = params.device_id || "";
  const code = params.code || "";

  return (
    <Suspense fallback={
      <div className="auth-container">
        <div className="auth-card animate-fade-in" style={{ textAlign: "center" }}>
          <div style={{ fontSize: "3rem", marginBottom: "1rem" }}>📡</div>
          <h1 className="auth-title">Connecting to Enclosure</h1>
          <p className="auth-subtitle" style={{ marginTop: "0.5rem" }}>
            Loading...
          </p>
        </div>
      </div>
    }>
      <ConnectClient deviceId={deviceId} code={code} />
    </Suspense>
  );
}