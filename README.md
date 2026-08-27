# Smart Key Storage System (SNTC)

**IIT Mandi SNTC — self-service key management with 2FA, proximity gating, and a full audit trail.**

A member walks up to the enclosure, proves they are physically there, authenticates
with password + TOTP, and the box dispenses their key. Every retrieval, return,
extension, override and tamper event is logged. Overdue keys escalate by email.

---

## Contents

- [Architecture](#architecture)
- [Repository layout](#repository-layout)
- [Local development](#local-development)
- [Environment variables](#environment-variables)
- [Deployment](#deployment)
- [API reference](#api-reference)
- [Authentication flow](#authentication-flow)
- [Why the WebSocket needs a ticket](#why-the-websocket-needs-a-ticket)
- [Notifications](#notifications)
- [Security](#security)
- [Testing](#testing)
- [Further documentation](#further-documentation)

---

## Architecture

```
Browser ──► Vercel (Next.js 14)
              │  /api/:path*  rewritten server-side
              ▼
           Render (FastAPI, Docker)
              ├─► Neon      PostgreSQL   durable state
              ├─► Upstash   Redis        sessions, rate limits, pub/sub
              ├─► HiveMQ    MQTT         ESP32-S3 enclosures  (optional)
              └─► SMTP                    notifications
```

| Piece | Stack |
|---|---|
| Frontend | Next.js 14 App Router, TypeScript, `output: "standalone"` |
| Backend | FastAPI 0.111, Python 3.12, SQLAlchemy 2.0 async + asyncpg, Alembic |
| Sessions & rate limits | Redis (server-side sessions, revocable instantly) |
| Realtime | Redis pub/sub → WebSocket `/ws/keys` |
| Devices | MQTT over TLS, single-use nonces on every unlock |
| Email | Provider pattern — `smtp` or `console` |

**The `/api` rewrite is load-bearing.** `next.config.js` proxies `/api/:path*` to
`INTERNAL_API_URL` *server-side*, so the browser only ever talks to the Vercel
origin. The backend's `SameSite=Lax` session cookie therefore looks first-party
and works unchanged across split hosting. Calling Render directly from the
browser would break the cookie — don't.

---

## Repository layout

```
backend/
  app/
    api/          routers: auth, keys, proximity, sessions, admin
    core/         config, database, redis_client, security
    email/        base + smtp_provider + console_provider + factory + templates
    models/       SQLAlchemy ORM (10 tables)
    schemas/      Pydantic v2 request/response
    services/     business logic (auth, key, proximity, admin, notification, mqtt)
    workers/      MQTT listener + APScheduler notification jobs
  alembic/        migrations
  tests/          unit + integration
  Dockerfile      production image (runs migrations, then uvicorn on $PORT)

frontend/
  app/            App Router pages
    login/          credentials → TOTP
    connect/        proximity code exchange (captive-portal redirect)
    keys/           key grid: retrieve / return / extend
    admin/          dashboard, users, rooms, permissions, logs, devices, reports
  components/     shared React components
  lib/api.ts      typed client covering every endpoint
  Dockerfile      multi-stage; `dev` target for compose hot reload

firmware/         ESP32-S3 enclosure sketch
docs/             PRD, TRD, architecture, user guide
render.yaml       Render blueprint for the backend
```

---

## Local development

### Option A — everything on the host

```bash
# Backend
cd backend
python3.12 -m venv .venv && .venv/bin/pip install -r requirements-dev.txt
cp .env.example .env            # fill in DATABASE_URL and REDIS_URL at minimum
.venv/bin/alembic upgrade head
.venv/bin/python seed.py        # demo users, rooms, key slots
.venv/bin/uvicorn app.main:app --reload

# Frontend (second terminal)
cd frontend
npm install
cp .env.example .env.local
npm run dev
```

- Frontend → http://localhost:3000
- Backend → http://localhost:8000
- Swagger → http://localhost:8000/docs (only when `DEBUG=true`)

### Option B — Docker Compose

```bash
docker compose up
```

`docker-compose.override.yml` loads automatically and rewrites the service
hostnames — `backend/.env` points at `localhost`, which is correct for a host
uvicorn and wrong inside a container. It also switches the frontend image to its
`dev` target so hot reload works.

### Local gotchas

- **`DEBUG=true` is required locally if `TOTP_DEMO_BYPASS_CODE` is set.** The app
  refuses to boot with the bypass armed while `DEBUG=false`, because that value is
  accepted as a valid TOTP code for *every* user. Fail at startup beats failing
  silently at the first login.
- **`EMAIL_PROVIDER=console`** logs mail instead of sending it. With `smtp` and no
  working credentials, `aiosmtplib` raises mid-request and key retrieval 500s even
  though the key dispensed fine.
- **`MQTT_ENABLED=false`** unless a broker is actually reachable. The listener
  otherwise retries every 5 s forever and buries real errors.

### Make targets

```bash
make dev          # docker compose up
make backend      # uvicorn --reload
make frontend     # next dev
make migrate      # alembic upgrade head
make migrate-new MSG="add_x_table"
make seed         # demo data
make test         # pytest
```

---

## Environment variables

### Backend (`backend/.env` — see `backend/.env.example`)

| Variable | Required | Default | Notes |
|---|---|---|---|
| `DATABASE_URL` | ✅ | — | `postgresql+asyncpg://…?ssl=require`. Neon hands you `postgresql://…?sslmode=require`; **both** parts must change — asyncpg doesn't understand libpq's `sslmode` and errors on the unknown keyword. |
| `REDIS_URL` | ✅ | — | `rediss://default:…@….upstash.io:6380`. Needs Redis ≥ 6.2 for `GETDEL`. |
| `ALLOWED_ORIGINS` | ✅ | `http://localhost:3000` | Comma-separated, scheme included. Credentialed CORS rejects `*`, so a missing entry fails every logged-in request. |
| `DEBUG` | | `false` | Gates `/docs`, `/redoc`, `/openapi.json`. |
| `EMAIL_PROVIDER` | | `smtp` | `smtp` \| `console` |
| `EMAIL_ADDRESS` / `EMAIL_APP_PASSWORD` | for `smtp` | — | Gmail app password, not the account password. |
| `SMTP_SERVER` / `SMTP_PORT` | | `smtp.gmail.com` / `587` | |
| `MQTT_ENABLED` | | `false` | Master switch for the listener. |
| `MQTT_HOST` / `MQTT_PORT` / `MQTT_USERNAME` / `MQTT_PASSWORD` / `MQTT_TLS` | for MQTT | `localhost` / `1883` / — / — / `false` | HiveMQ Cloud is TLS-only on 8883 and rejects anonymous connections. |
| `SESSION_TTL_SECONDS` | | `3600` | Also sets the cookie `max-age`. |
| `PROXIMITY_CODE_TTL_SECONDS` | | `120` | |
| `PROXIMITY_FLAG_TTL_SECONDS` | | `300` | How long proximity stays "fresh". |
| `LOGIN_MAX_ATTEMPTS` / `LOGIN_LOCKOUT_SECONDS` | | `5` / `900` | |
| `TOTP_MAX_ATTEMPTS` / `TOTP_LOCKOUT_SECONDS` | | `5` / `300` | |
| `WS_TICKET_TTL_SECONDS` | | `30` | A ticket is redeemed within a page load or not at all. |
| `REMINDER_BEFORE_DUE_MINUTES` | | `30` | |
| `ESCALATION_AFTER_DUE_HOURS` | | `2` | |
| `DEFAULT_POSSESSION_HOURS` | | `6` | |
| `TOTP_DEMO_BYPASS_CODE` | | *(empty)* | **Local demo only.** Refuses to boot unless `DEBUG=true`. |

### Frontend (`frontend/.env.local` — see `frontend/.env.example`)

| Variable | When | Notes |
|---|---|---|
| `INTERNAL_API_URL` | server-side, runtime | Target of the `/api/:path*` rewrite. The **production build fails outright** if it's unset — a bundle silently pointing at localhost 502s in production with nothing in the logs to explain it. |
| `NEXT_PUBLIC_API_URL` | build time, inlined | Public backend origin. Only used to build the `wss://` URL for `/ws/keys`, which cannot go through the rewrite. Empty locally falls back to the current host. |
| `NEXT_PUBLIC_DEMO_MODE` / `NEXT_PUBLIC_DEMO_EMAIL` / `NEXT_PUBLIC_DEMO_PASSWORD` | optional | Login-page autofill. **Leave unset in production** — anything `NEXT_PUBLIC_*` ends up in the public JavaScript bundle. |

---

## Deployment

| Component | Platform |
|---|---|
| FastAPI backend | Render (Docker) |
| Next.js frontend | Vercel |
| PostgreSQL | Neon |
| Redis | Upstash |
| MQTT | HiveMQ Cloud, or self-hosted Mosquitto |

### 1. Neon

Create a project, copy the connection string, then rewrite it for asyncpg:

```
postgresql://u:p@ep-x.aws.neon.tech/kms?sslmode=require      ← what Neon gives you
postgresql+asyncpg://u:p@ep-x.aws.neon.tech/kms?ssl=require  ← what DATABASE_URL needs
```

### 2. Upstash

Create a Redis database and copy the `rediss://` URL. The default TLS endpoint is
fine. `/ws/keys` tickets use `GETDEL`, which needs Redis ≥ 6.2 — Upstash supports it.

### 3. Render

`render.yaml` is a blueprint: point Render at the repo and it picks it up.

- Runtime **Docker**, `dockerfilePath: ./backend/Dockerfile`, `dockerContext: ./backend`
- Health check `/health`
- `numInstances: 1` — the scheduler and MQTT listener run in-process and the
  Dockerfile runs `alembic upgrade head` on boot. A second instance would duplicate
  every scheduled notification and race the migration.
- Secrets are declared `sync: false`, so Render prompts on first deploy and stores
  them itself. Nothing secret is committed.

Set at deploy time: `DATABASE_URL`, `REDIS_URL`, `ALLOWED_ORIGINS`,
`EMAIL_ADDRESS`, `EMAIL_APP_PASSWORD`.

> **Free tier spins down after ~15 min idle**, which also stops APScheduler, so
> reminders would silently stop firing. Point an external cron at
> `https://<service>.onrender.com/health` every 10 minutes to keep it warm.

### 4. Vercel

- **Root directory: `frontend`** (monorepo — this is the one setting people miss)
- Environment variables:
  - `INTERNAL_API_URL` = `https://<service>.onrender.com`
  - `NEXT_PUBLIC_API_URL` = `https://<service>.onrender.com`
- Leave `NEXT_PUBLIC_DEMO_*` unset.

### 5. Close the loop

Add the Vercel URL to the backend's `ALLOWED_ORIGINS` and redeploy Render.
Credentialed CORS has no wildcard, so this step is not optional.

### Self-hosted alternative

`docker-compose.prod.yml` brings up the whole stack — Postgres, Redis, Mosquitto,
backend, frontend — with no source mounts. It needs a `.env` beside it with
`POSTGRES_USER`, `POSTGRES_PASSWORD` and `PUBLIC_API_URL`.

---

## API reference

31 HTTP routes plus one WebSocket. `frontend/lib/api.ts` is a typed client for all
of them. Interactive docs at `/docs` when `DEBUG=true`; full prose reference in
[`docs/TRD.md`](docs/TRD.md).

**Auth** — `POST /auth/login`, `/auth/totp/setup`, `/auth/totp/verify`,
`/auth/logout`, `/auth/ws/ticket`

**Proximity** — `POST /proximity/verify`

**Sessions** — `POST /sessions/start`, `POST /sessions/{id}/close` *(device-called)*

**Keys** — `GET /keys`, `POST /keys/{slot_id}/retrieve` · `/return` · `/extend`

**Admin** — `GET /admin/dashboard`; users (`GET`/`POST`/`PATCH`, `bulk-import`,
`totp/reenroll`); permissions (`POST`, `DELETE`); rooms (`GET`, `POST`); logs
(`access`, `retrieval`, `override`, `override/{id}/resolve`); devices (`GET`,
`POST`, `{id}/maintenance`); `GET /admin/reports/usage`

**Ops** — `GET /health`, `WS /ws/keys?ticket=…`

---

## Authentication flow

Proximity verification needs a session, so login comes first. The code proves
physical presence because it is only obtainable inside the enclosure's radio range.

1. User joins the enclosure WiFi → captive portal shows a short-lived code
2. User rejoins their normal network (the enclosure AP has no internet route)
3. `POST /auth/login` → `temp_token`
4. `POST /auth/totp/setup` (first time only, authorised by the temp token) → QR + secret
5. `POST /auth/totp/verify` → HttpOnly session cookie
6. `/connect` posts the code to `POST /proximity/verify` → 5-minute proximity flag
7. `POST /sessions/start` → unlocks the door (proximity-gated)
8. `POST /keys/{slot}/retrieve` → dispenses the key (proximity-gated + permission-checked)

An already-signed-in user on the enclosure WiFi can be redirected straight to
`/connect?device_id=X&code=Y`, skipping the manual entry at step 6.

Walkthrough, Google Authenticator enrollment and the ESP32 firmware contract:
[`docs/USER_GUIDE.md`](docs/USER_GUIDE.md).

---

## Why the WebSocket needs a ticket

Every HTTP call rides the same-origin `/api` proxy, so the session cookie applies.
`/ws/keys` cannot: Vercel rewrites do not upgrade WebSockets, so the browser opens
the socket **directly against Render**. The cookie was set through the proxy and
therefore belongs to the *Vercel* host — it can never be sent to `*.onrender.com`
at any `SameSite` value, and `HttpOnly` stops JavaScript forwarding it by hand.

So the client mints a ticket over the authenticated proxy and spends it on the
handshake:

```
POST /auth/ws/ticket   (cookie applies — goes through /api)  → { ticket }
WS   /ws/keys?ticket=…  (direct to Render)                   → redeemed via GETDEL
```

Tickets live 30 seconds and `GETDEL` burns them atomically, so two concurrent
redemptions cannot both win. Missing, unknown and expired all close with `1008`,
so a caller can't probe which tickets once existed. If the socket never connects
the page still works — it polls after every action and has a Refresh button.

---

## Notifications

| Event | Timing | Recipient |
|---|---|---|
| Retrieval confirmation | immediate | member |
| Return reminder | T−30 min | member |
| Overdue warning | at due time | member |
| Coordinator escalation | T+2 h | coordinator |
| Tamper alert | immediate | all admins |

Driven by APScheduler inside the API process — hence `numInstances: 1` and the
health-check cron.

---

## Security

- **2FA** — bcrypt password + TOTP (RFC 6238, ±1 step)
- **Sessions** — server-side in Redis, revocable instantly; cookie is `HttpOnly`,
  `Secure`, `SameSite=Lax`, with `max-age` pinned to the Redis TTL
- **Proximity gate (FR-7)** — retrieve / return / start-session all require a fresh
  proximity flag
- **Rate limiting** — 5 login failures / 15 min, 5 TOTP failures / 5 min
- **Replay protection (FR-3)** — every MQTT unlock command carries a single-use nonce
- **RBAC** — admin sees all, coordinator sees their rooms, member sees their own keys
- **Startup guards** — the app refuses to boot with the TOTP bypass armed outside
  debug; the production frontend build fails if `INTERNAL_API_URL` is missing
- **No secrets in the repo** — only `.env.example` files are tracked, and git
  history has never contained a real `.env`

---

## Testing

```bash
cd backend
.venv/bin/pip install -r requirements-dev.txt
.venv/bin/pytest tests/ -v

cd ../frontend
npx tsc --noEmit
```

`requirements.txt` is production-only; the test framework lives in
`requirements-dev.txt` so it never ships in the Render image.

---

## Further documentation

| Document | Contents |
|---|---|
| [`docs/PRD.md`](docs/PRD.md) | Product requirements, user stories |
| [`docs/TRD.md`](docs/TRD.md) | Full API reference, data model, MQTT contract |
| [`docs/architecture.md`](docs/architecture.md) | System design, deployment topology |
| [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md) | End-user walkthrough, TOTP enrollment, firmware |
</content>
</invoke>
