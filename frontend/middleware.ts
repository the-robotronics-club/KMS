import { NextResponse } from "next/server";
import type { NextRequest } from "next/server";

export function middleware(request: NextRequest) {
  const session = request.cookies.get("session_id");
  const { pathname } = request.nextUrl;

  // Protected paths
  // /totp/setup is deliberately absent: it runs before a session exists, and
  // authorises itself with the temp_token issued by /auth/login.
  const isProtectedPath = pathname.startsWith("/keys") || pathname.startsWith("/admin");
  const isGuestOnlyPath = pathname === "/login" || pathname === "/login/totp";

  // Redirect to login if accessing protected path without session
  if (isProtectedPath && !session) {
    const loginUrl = new URL("/login", request.url);
    return NextResponse.redirect(loginUrl);
  }

  // Redirect to dashboard/keys if logged in and trying to access guest paths
  if (isGuestOnlyPath && session) {
    const keysUrl = new URL("/keys", request.url);
    return NextResponse.redirect(keysUrl);
  }

  return NextResponse.next();
}

export const config = {
  // Apply middleware to all app routes except static files, api routes, and public folder assets
  matcher: [
    "/((?!api|_next/static|_next/image|favicon.ico|connect|public).*)",
  ],
};
