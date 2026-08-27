# ESP32 enclosure firmware

Board: **ESP32 DevKit V1** (ESP32-WROOM-32). Pin map below is for that board.

## What it does

| Job | Detail |
|---|---|
| SoftAP `SNTC-Enclosure` | Serves a captive portal showing a 6-char proximity code |
| STA on campus WiFi | Uplink for MQTT — the AP itself has **no** internet route |
| Publishes the code | `device/{uuid}/access/proximity_code`, rotated every 5 min |
| Obeys commands | `unlock_door`, `dispense`, `unlock` — with nonce replay protection |
| Reports | door events, tamper, telemetry, heartbeat every 30 s |

The AP is deliberately a dead end. Members read the code, leave the AP, and
submit it from a real network. Full rationale in `docs/USER_GUIDE.md` §2.

## Cabinet ESP32 + Arduino Uno network handoff

For the cabinet gateway that drives the Arduino Uno over UART, use
`firmware/cabinet_esp`. It runs the ESP access point and upstream Wi-Fi at the
same time, but the access point does **not** provide internet routing to phones.

**The phone is not the transport.** One phone radio cannot be on
`ESP-KMS-xxxxxx` and on the internet at once, so the cabinet carries its own
uplink: the backend publishes dispense commands to the broker and the ESP32
picks them up over MQTT on its station interface. The phone joins the AP for
exactly one thing — reading the proximity code off the portal — then leaves and
uses mobile data for everything else.

```
phone (mobile data) → kms-opal.vercel.app → Render FastAPI
                                              ↓ MQTT/TLS 8883
                                           HiveMQ Cloud
                                              ↓
                          cabinet ESP32 (STA) → UART 9600 → Arduino Uno
```

Two independent gates, both still required: the authenticator/TOTP code proves
*who* you are and works from anywhere; the proximity code proves *where* you are
and only the cabinet knows it.

### Configure

Copy `firmware/cabinet_esp/secrets.h.example` to `secrets.h` (gitignored) and
fill in what differs from `config.h`:

```c
#define STA_SSID      "your-upstream-wifi"
#define STA_PASSWORD  "your-upstream-password"
#define MQTT_HOST     "xxxxx.s1.eu.hivemq.cloud"
#define MQTT_USERNAME "kms-cabinet"
#define MQTT_PASSWORD "..."
#define DEVICE_UUID   "..."      // only if you made your own device row
```

Everything in `config.h` is `#ifndef`-guarded, so an absent define keeps the
default. `MQTT_ROOT_CA` pins ISRG Root X1 (HiveMQ serves Let's Encrypt certs) —
not `setInsecure()`, because an unverified socket lets anyone forge `dispense`
and empty the rack. Re-check the chain before flashing a fleet:

```bash
openssl s_client -connect <host>:8883 -showcerts </dev/null
```

### Where the rack geometry lives

The ESP32 sends `SLOT:<n>` and knows nothing else about the mechanics. Slot
count, microstepping and the slot-to-step arithmetic all live in
`firmware/KMS_LowLevel_ArduinoUno/rack_geometry.h`, on the board that drives the
motor. Re-gear the rack, move a belt, or change the TB6600 DIP switches and only
the Uno is reflashed.

The rack is a **24-slot carousel**: one stepper, one solenoid, 15° between
adjacent keys. `SLOT_COUNT` appears in three places and all three must agree —
`rack_geometry.h`, `cabinet_esp/config.h` (used only to reject a bad slot early),
and the `key_slots` CHECK constraint in the database.

Prove the arithmetic on your laptop before touching hardware:

```bash
gcc -Wall -Wextra -o /tmp/trg firmware/tools/test_rack_geometry.c && /tmp/trg
```

That test exists because the obvious formula is wrong. `(STEPS_PER_REV /
SLOT_COUNT) * index` truncates `1600 / 24` to `66` and loses a third of a step
per slot — 0.45° out by slot 3, and 3.6° out by slot 24, where a full revolution
never closes. Multiply first, divide last.

### Campus captive portal

The IIT Mandi network associates with no 802.1X and then intercepts every
request until its login form is POSTed, so *associated* is not *online*. The
firmware probes `connectivitycheck.gstatic.com/generate_204` and logs in when the
probe comes back as anything other than a bare 204.

Leave `PORTAL_LOGIN` at `0` for a network that just works once it associates —
home Wi-Fi, a phone hotspot, a 4G MiFi.

The portal is a **FortiGate**, captured off the live network rather than guessed,
so `config.h` already carries the right endpoints, field names and TLS root. Only
three defines belong in `secrets.h`:

```c
#define PORTAL_LOGIN      1
#define PORTAL_USERNAME   "your-ldap-id"
#define PORTAL_PASSWORD   "your-ldap-password"
```

Login is a **two-step exchange**, not one POST. The form carries a hidden
per-session `magic` token that rotates on every GET, so the firmware GETs
`https://login.iitmandi.ac.in:1003/login?`, scrapes the magic, then POSTs
`4Tredir` + `magic` + credentials to `https://login.iitmandi.ac.in:1003/`. A
stale magic is rejected exactly as a wrong password is. All four values are
percent-encoded, so a password containing `&`, `+` or `%` is safe. Both paths
have a host self-check — no hardware needed:

```bash
gcc -Wall -Wextra -o /tmp/tu firmware/tools/test_urlencode.c && /tmp/tu
gcc -Wall -Wextra -o /tmp/ts firmware/tools/test_portal_scrape.c && /tmp/ts
```

The TLS leg is **verified**, not blind: `PORTAL_ROOT_CA` pins Go Daddy Root
Certificate Authority - G2, which the portal's `*.iitmandi.ac.in` cert chains to.
Never swap it for `setInsecure()` — an unverified socket hands the campus account
to anyone who can answer on port 1003. If the campus ever changes CA, this fails
first and tells you which root to paste in:

```bash
openssl s_client -connect login.iitmandi.ac.in:1003 \
  -servername login.iitmandi.ac.in -CAfile gdroot.pem -no-CAstore -no-CApath
```

Serial output on a portal network, at 115200:

```
[portal] traffic intercepted, logging in
[portal] login POST -> 200
[portal] session open
[mqtt] connected
```

FortiGate answers **200 for a rejected login too** and just re-serves the form,
so the HTTP status is not the verdict — the re-probe is. A bad credential reads:

```
[portal] login POST -> 200 (credential rejected)
[portal] still blocked
```

One thing worth knowing before you flash a personal credential: flash reads out
over USB with `esptool.py read_flash`. No soldering, no exploit. Whoever opens
the cabinet gets a working campus account, and if that account is SSO they get
mail with it. Ask IT for a service account or an IoT MAC exemption first; failing
that, rotate the credential when the project ends.

The login only runs while MQTT is down. A live MQTT session is itself proof the
portal session is open, and its keepalives are what hold it open — so in steady
state this costs nothing.

### Render side

`render.yaml` sets `MQTT_ENABLED=true`, `MQTT_PORT=8883`, `MQTT_TLS=true`, and
prompts for `MQTT_HOST` / `MQTT_USERNAME` / `MQTT_PASSWORD` on first deploy. Use
a **separate** broker credential from the cabinet's so either can be revoked
alone.

### Arduino Uno side

The sketch listens on **both** the ESP32 link and the USB serial monitor, at all
times. There is no flag to set and no mode to be in the wrong one of.

This used to be a `DEBUG_VIA_USB` switch that chose between the two, and it
shipped set to `true` — meaning the Uno read USB and never `commSerial`, so
every command the ESP32 sent was discarded with no error at either end. The
dispense path was dead and the only symptom was "the ESP isn't talking to the
Arduino". If you are resurrecting an older sketch and the link seems dead, that
flag is the first thing to check.

`VERBOSE_LOG` still exists but only controls how chatty the USB log is. It
cannot break the link.

### Test it in layers

```bash
# 1. pure-C headers, on the host — no hardware
gcc -o /tmp/tp  firmware/tools/test_protocol.c      && /tmp/tp
gcc -o /tmp/tn  firmware/tools/test_nonce.c         && /tmp/tn
gcc -o /tmp/trg firmware/tools/test_rack_geometry.c && /tmp/trg

# 2. Uno alone, over USB — no ESP32 needed, no flag to flip
#    send SLOT:7 -> expect ACK:SLOT then DONE:SLOT, and the platter moves
#    send SLOT:0 or SLOT:99 -> expect ERR:BAD_SLOT and no movement

# 3. ESP alone: watch it publish
mosquitto_sub -h <host> -p 8883 --capath /etc/ssl/certs \
              -u <user> -P <pass> -t 'device/#' -v

# 4. command path, without the website. The nonce is a STRING — the backend
#    mints it with secrets.token_hex(16), and a bare number here would not
#    exercise the path the website actually uses.
mosquitto_pub -h <host> -p 8883 --capath /etc/ssl/certs \
              -u <user> -P <pass> \
              -t 'device/<uuid>/rack/command' \
              -m '{"action":"dispense","slot_number":3,"nonce":"a3f9c2d1e4b5a6978877665544332211"}'
```

Expect `[mqtt] dispense slot 3` on serial. `[mqtt] no nonce, dropped` means the
payload carried no nonce the firmware could parse; `[mqtt] replay <nonce>` means
that exact nonce is already in the 64-entry ring — mint a new one.
`"action":"unlock"` (a key *return*) drives the same platter move.

Then the full path with the phone on mobile data for everything except the code
read.

## Flash it

1. **Arduino IDE** → Preferences → Additional Board URLs:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
   Then Boards Manager → install **esp32** by Espressif.

2. Library Manager → install:
   - `PubSubClient` (Nick O'Leary)
   - `ArduinoJson` (Benoit Blanchon, v7+)

3. Edit `config.h`:
   ```c
   #define STA_SSID      "your-campus-ssid"
   #define STA_PASSWORD  "your-campus-password"
   #define MQTT_HOST     "192.168.1.50"   // machine running docker-compose
   ```
   `DEVICE_UUID` already matches the seeded demo device — leave it alone unless
   you created your own device in Admin → Devices.

4. Board: **DOIT ESP32 DEVKIT V1**. Select the port, Upload.

Or skip the IDE entirely — `arduino-cli` is scriptable:

```bash
# cabinet ESP32 (the gateway)
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/cabinet_esp
arduino-cli upload  --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 firmware/cabinet_esp
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200

# Arduino Uno (the motor board)
arduino-cli compile --fqbn arduino:avr:uno firmware/KMS_LowLevel_ArduinoUno
arduino-cli upload  --fqbn arduino:avr:uno -p /dev/ttyACM0 firmware/KMS_LowLevel_ArduinoUno
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=9600
```

Serial access needs your user in `dialout`: `sudo usermod -aG dialout $USER`,
then log out and back in.

5. Serial Monitor at **115200**. Expected:
   ```
   [ap] SNTC-Enclosure at 4.3.2.1
   [sta] up: 192.168.1.77
   [code] initial 4F2A91
   [mqtt] connected
   [code] 4F2A91 -> device/1111.../access/proximity_code
   ```

## Wiring

**Never use GPIO 6–11 on a WROOM-32 module** — they are wired to the internal
SPI flash, and driving them crashes the chip into a boot loop.

### Cabinet ESP32 (WROOM-32)

| Signal | GPIO | Notes |
|---|---|---|
| Door lock driver | 4 | HIGH = unlocked. Use a MOSFET/relay, not the pin directly |
| Tamper switch | 5 | NC to GND; opens (reads HIGH) when the enclosure is prised |
| UART TX → Uno pin 2 | 17 | 3.3 V out. The Uno reads this fine as-is |
| UART RX ← Uno pin 3 | 16 | **Needs a level shifter** — the Uno drives 5 V into a 3.3 V pin |
| GND ↔ Uno GND | — | Not optional. A missing common ground is the single most common reason the link appears dead |

### Arduino Uno (motor board)

| Signal | Pin | Notes |
|---|---|---|
| SoftwareSerial RX ← ESP32 TX | 2 | Pins 0/1 are the USB port, hence SoftwareSerial |
| SoftwareSerial TX → ESP32 RX | 3 | Through the level shifter |
| TB6600 PUL+ | 6 | `STEPPER_STEP_PIN` |
| TB6600 DIR+ | 7 | `STEPPER_DIR_PIN`. PUL−/DIR− go to GND |
| Solenoid relay | 4 | Drops one key. 5 s coil-duty ceiling enforced in firmware |
| Battery tap | A0 | Through a divider; set `VBAT_DIVIDER_RATIO` to match |

There is **no home or limit switch**. The Uno counts steps from wherever it
powered up, so if it resets mid-shift every slot number points at the wrong key
and nothing in software can tell. Park the rack at slot 1 before power-cycling,
or add a switch.

Change the `PIN_*` constants at the top of each sketch if your board differs.

## Testing without hardware

The whole proximity path can be exercised from a laptop — publish a code by
hand and it lands in Redis exactly as the ESP32's would:

```bash
docker compose up -d mosquitto redis postgres

mosquitto_pub -h localhost -t 'device/11111111-2222-3333-4444-555555555555/access/proximity_code' \
              -m '{"code":"TEST42"}'
```

Then enter `TEST42` at `/connect` in the portal. Watch commands go the other
way with:

```bash
mosquitto_sub -h localhost -t 'device/#' -v
```

## MQTT broker choice

- **Local dev** — `docker compose up mosquitto`, set `MQTT_HOST` to your
  machine's LAN IP (not `localhost`; the ESP32 resolves that to itself).
- **Production** — HiveMQ Cloud. Set `MQTT_USERNAME`/`MQTT_PASSWORD`, and note
  that TLS on port 8883 needs `WiFiClientSecure` instead of `WiFiClient`.

## Known limits

- `openDoor()` blocks for the unlock TTL. Fine for one door; add a timer if you
  ever actuate concurrently.
- Battery telemetry is hardcoded to 100%. Wire a fuel gauge and replace it.
- No OTA. Re-flash over USB.
