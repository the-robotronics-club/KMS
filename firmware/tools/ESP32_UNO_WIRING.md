# KMS Bench Test Guide — Wiring, UART-only, and Full-Stack

Single reference for testing the Arduino Uno low-level controller together with the
ESP32 (`cabinet_esp`), and optionally the full backend + frontend + MQTT stack.

Two tiers, do them in order:
- **Tier 1** — just prove Uno <-> ESP32 wiring/protocol works. No backend, no MQTT, no internet.
- **Tier 2** — full stack: frontend -> backend -> MQTT -> ESP32 -> UART -> Uno -> motor/solenoid.

---

## Part A — Physical wiring (needed for both tiers)

Pin sources: `firmware/KMS_LowLevel_ArduinoUno/src/KMS_LowLevel_ArduinoUno.ino` (top config
block) and `firmware/cabinet_esp/config.h` (UART section).

### A.1 — UART link (Uno <-> ESP32) — the only cross-board connection

| Signal              | Uno pin | ESP32 pin | Notes |
|----------------------|---------|-----------|-------|
| Uno RX  <- ESP32 TX  | D2      | GPIO17    | 3.3V into Uno's 5V-tolerant RX is fine direct, but route through the shifter anyway for consistency. |
| Uno TX  -> ESP32 RX  | D3      | GPIO16    | **MUST** go through a level shifter. Uno outputs 5V logic; ESP32 GPIOs are 3.3V-only and can be damaged by sustained 5V. |
| GND                  | GND     | GND       | Common ground is mandatory. |

Baud: **9600** both ends (`COMM_BAUD_RATE` in both configs — already matches; don't change one without the other).

Level shifter wiring (any bidirectional 2-channel module, e.g. TXS0108E/BSS138 breakout):
- HV side -> Uno 5V + GND
- LV side -> ESP32 3.3V + GND
- Uno D3 -> shifter HV1 -> shifter LV1 -> ESP32 GPIO16
- ESP32 GPIO17 -> shifter LV2 -> shifter HV2 -> Uno D2

### A.2 — Uno's own peripherals (already wired if you've been bench-testing via USB)

| Function              | Uno pin | Connects to |
|------------------------|---------|-------------|
| Stepper STEP (PUL+)    | D6      | TB6600 driver PUL+ |
| Stepper DIR (DIR+)     | D7      | TB6600 driver DIR+ |
| Stepper PUL-/DIR-      | GND     | TB6600 driver PUL-/DIR- (common ground wiring) |
| Solenoid relay control | D4      | Relay module IN |
| Battery sense          | A0      | Resistor divider off battery rail (`VBAT_DIVIDER_RATIO` in the .ino) |

### A.3 — Power

- **Uno**: USB (for monitoring) or a separate 5-12V supply. Fine to have both at once. Do NOT power the Uno from the ESP32's rail or vice versa — only grounds are shared, never power, across the level shifter.
- **ESP32**: its own USB or 3.3V/5V supply per the dev board.
- **TB6600 + motor**: separate motor power supply (12-24V typical), never shared with logic rails.
- **Solenoid relay**: check its coil supply requirement (5V/12V) — many relay modules draw more than the Uno's onboard regulator should supply, especially with the motor also running.

### A.4 — Sanity checks before joining the two boards

1. Power the Uno alone via USB, open its serial monitor, confirm `SLOT:7` etc. still work (validates firmware independent of ESP32).
2. Power the ESP32 alone via its own USB, watch its serial log, confirm it boots and doesn't error on UART init.
3. Only then wire the A.1 cross-connection and power both.

---

## Part B — Tier 1: UART-only test (no backend, no MQTT)

Goal: prove the physical link and ASCII protocol work, nothing more.

1. Wire per Part A. Power both boards.
2. Open the Uno's USB serial monitor (`pio device monitor --port <UnoPort>`).
3. `cabinet_esp` polls the Uno on its own, independent of MQTT/Wi-Fi state — every
   `POLL_INTERVAL_MS` (2000ms, `cabinet_esp/config.h:190`) it sends `BATT:?` and `STATUS:?`
   over UART to keep its local status cache fresh. You should see these arrive on the Uno's
   log **by themselves**, with nothing typed by you:
   ```
   [CMD] BATT:?
   [RESP] BATT:<n>
   [CMD] STATUS:?
   [RESP] STATUS:IDLE
   ```
   If you see this, the wiring and baud rate are correct — the ESP32 is genuinely talking
   to the Uno over UART.
4. To test a real `SLOT`/`ACTUATE` command without any backend, use the same firmware's
   own path: the ESP32 will only send those in response to an MQTT `rack/command` message,
   so to test end-to-end without MQTT, run a local Mosquitto broker and publish to it
   directly (skips the backend entirely) — see Part C.2 step 4 for the exact `mosquitto_pub`
   command. Point `cabinet_esp`'s `MQTT_HOST`/`MQTT_TLS`/`MQTT_PORT` at that same local
   broker (Part C.1) for this to work.
5. If nothing shows up on the Uno's log at all: check the level shifter orientation (HV
   vs LV sides swapped is the most common mistake), confirm common ground, and confirm
   both boards show `9600` baud in their respective configs.

---

## Part C — Tier 2: full stack (frontend + backend + MQTT + both boards)

### C.1 — MQTT broker: local Mosquitto (simplest for bench testing, no cloud account needed)

Per `firmware/cabinet_esp/config.h:211-212` and `firmware/README.md:304-319`, local
Mosquitto is an explicitly supported swap-in for HiveMQ Cloud:

- Run it (already in the repo's docker compose): `docker compose up -d mosquitto`
- It listens on plaintext port **1883** on your LAN IP (not `localhost` — the ESP32 can't
  resolve that to your machine, it needs your actual LAN IP address, e.g. `192.168.1.50`).
- Set the same `MQTT_USERNAME`/`MQTT_PASSWORD`/`MQTT_DEVICE_PASSWORD` on both the backend
  `.env` and the ESP32's `secrets.h` (see C.2 and C.3).

### C.2 — Backend setup

```
cd backend
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt -r requirements-dev.txt
copy .env.example .env
```

Edit `.env`:
- `DATABASE_URL` — point at a Postgres instance (Neon works, or a local Postgres)
- `REDIS_URL` — Redis instance (Upstash or local)
- `MQTT_ENABLED=true`
- `MQTT_HOST=<your LAN IP running mosquitto>`, `MQTT_PORT=1883`, `MQTT_TLS=false`
- `MQTT_USERNAME` / `MQTT_PASSWORD` / `MQTT_DEVICE_PASSWORD` — match whatever Mosquitto
  is configured to accept (or leave open/anonymous for a bare bench broker)
- `TOTP_ENCRYPTION_KEY` — required, app refuses to start without it; generate any Fernet key
- `ALLOWED_ORIGINS` — must match the frontend origin (e.g. `http://localhost:3000`)

Then:
```
alembic upgrade head
python seed.py
uvicorn app.main:app --reload --port 8000
```

`seed.py` creates a fixed-UUID device (`11111111-2222-3333-4444-555555555555`) matching
`DEVICE_UUID` in `firmware/cabinet_esp/config.h` — don't change one without the other.

**Manual test of the command path without the frontend at all**, once Mosquitto + backend
are up:
```
mosquitto_sub -t 'device/#' -v
mosquitto_pub -t 'device/11111111-2222-3333-4444-555555555555/rack/command' \
  -m '{"action":"dispense","slot_number":3,"nonce":"test123"}'
```
Watch the ESP32's own serial log and the Uno's serial log — you should see the ESP32
receive the MQTT message and immediately send `SLOT:3` then `ACTUATE:1`/`ACTUATE:0` over
UART to the Uno.

### C.3 — ESP32 (`cabinet_esp`) setup

In `firmware/cabinet_esp/`, copy `secrets.h.example` to `secrets.h` and set:
- `STA_SSID` / `STA_PASSWORD` — Wi-Fi network reachable by both the ESP32 and your Mosquitto/backend machine
- `MQTT_HOST` / `MQTT_PORT=1883` / `MQTT_TLS=0` — same values as backend's `.env`
- `MQTT_USERNAME` / `MQTT_PASSWORD`

Build + flash via PlatformIO. Watch its serial log to confirm: Wi-Fi connects, MQTT
connects, and (once wired to the Uno) UART polling succeeds.

### C.4 — Frontend setup

```
cd frontend
npm install
copy .env.example .env.local
```
Edit `.env.local`:
- `INTERNAL_API_URL=http://localhost:8000`
- `NEXT_PUBLIC_API_URL=http://localhost:8000`
- optionally `NEXT_PUBLIC_DEMO_MODE=true` with `DEMO_EMAIL`/`DEMO_PASSWORD` to autofill login

```
npm run dev
```
Opens on `http://localhost:3000`.

### C.5 — End-to-end test flow via the actual UI

1. Log in (TOTP step if 2FA is enabled on the seeded user).
2. Go to the `keys` page — lists slots for the seeded device, live-updates over WebSocket.
3. A proximity code must be cached first: the ESP32 publishes one periodically
   (`TOPIC_CODE`, rotates every 5 min per `CODE_ROTATE_MS`) — this only happens once the
   ESP32 is actually connected to the broker, so confirm C.3 succeeded first.
4. Pick a slot and hit "retrieve" — this calls `POST /keys/{slot_id}/retrieve` on the
   backend, which publishes to `device/<uuid>/rack/command`, which the ESP32 relays to the
   Uno as `SLOT:<n>` then `ACTUATE:1`/`ACTUATE:0`.
5. Confirm on the Uno's serial log that the commands arrived and the motor/solenoid
   physically responded.

---

## Troubleshooting quick reference

- **Nothing arrives on Uno's log at all** -> UART wiring/level-shifter issue (Part A), or
  ESP32 never got that far in boot — check its own serial log first.
- **ESP32 boots but never connects to Mosquitto** -> LAN IP wrong (not `localhost`),
  firewall blocking 1883, or `MQTT_USERNAME`/`PASSWORD` mismatch between ESP32 secrets.h
  and Mosquitto's config.
- **Backend won't start** -> almost always `TOTP_ENCRYPTION_KEY` missing, or `DATABASE_URL`
  using `sslmode` instead of `ssl=require` for asyncpg.
- **Retrieve button fails with a proximity error** -> the ESP32 hasn't published a fresh
  code yet, or too much time passed since the last one (`CODE_ROTATE_MS` / backend's
  `PROXIMITY_CODE_TTL_SECONDS` mismatch).
