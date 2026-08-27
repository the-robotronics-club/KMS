/** @type {import('next').NextConfig} */
const nextConfig = {
  reactStrictMode: true,
  // Emits .next/standalone/server.js — what the Dockerfile runner stage copies.
  output: "standalone",
  // Drops the `X-Powered-By: Next.js` response header.
  poweredByHeader: false,
  // Already the default; pinned because shipping maps hands over unminified source.
  productionBrowserSourceMaps: false,
  async headers() {
    return [
      {
        source: "/:path*",
        headers: [
          { key: "X-Content-Type-Options", value: "nosniff" },
          { key: "X-Frame-Options", value: "DENY" },
          { key: "Referrer-Policy", value: "no-referrer" },
          {
            key: "Permissions-Policy",
            value: "camera=(), microphone=(), geolocation=()",
          },
        ],
      },
    ];
  },
  async rewrites() {
    return [
      {
        source: "/api/:path*",
        // Server-side only. INTERNAL_API_URL is a Vercel environment variable in
        // production and the compose-network address in Docker; NEXT_PUBLIC_API_URL
        // is what the browser bundle needs and is deliberately not read here — it
        // can't be, it's inlined at build time.
        //
        // Keeping /api same-origin is what makes the session cookie work: the
        // backend sets it SameSite=Lax, so a cross-origin XHR would drop it.
        destination: `${process.env.INTERNAL_API_URL || "http://localhost:8000"}/:path*`,
      },
    ];
  },
};

// Rewrites are baked in at build time, so a missing INTERNAL_API_URL silently
// ships a bundle pointing at localhost — every /api call then 502s in production
// with nothing in the logs explaining why. Fail the build instead.
if (process.env.NODE_ENV === "production" && !process.env.INTERNAL_API_URL) {
  throw new Error(
    "INTERNAL_API_URL must be set for a production build (e.g. https://sntc-kms-api.onrender.com)"
  );
}

module.exports = nextConfig;
