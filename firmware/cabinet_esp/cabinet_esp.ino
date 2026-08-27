// ─────────────────────────────────────────────────────────────────────────────
// KMS high-level (cabinet) ESP32
//
// Three jobs:
//
//   1. SoftAP + captive portal + LittleFS web UI. The portal shows the current
//      proximity code and then hands the phone off to the deployed website. The
//      AP has NO internet route on purpose.
//   2. MQTT uplink over the station interface. This is what stops the phone
//      being the transport: the backend publishes dispense commands to the
//      broker and the cabinet picks them up on its own internet connection, so
//      the phone only ever needs internet and never needs to be on the ESP AP.
//      On a campus network that hijacks traffic until its login form is POSTed,
//      the portal login runs first — see the captive portal section.
//   3. UART bridge to the Arduino Uno that drives the stepper and solenoid.
//
// Nothing in the MQTT path blocks. dnsServer.processNextRequest() and the UART
// poll have to keep running while the broker is unreachable, so the dispense
// sequence is a state machine and reconnects use a backoff timer.
//
// What changed from the original sketch is everything behind /api:
//
//   - /api/action now speaks the plaintext protocol.h line format accepted by
//     the Arduino Uno low-level controller.
//   - JSON bodies are read through AsyncCallbackJsonWebHandler. The original
//     used request->getParam("plain", true), which only exists for
//     application/x-www-form-urlencoded, so a real application/json POST fell
//     through to the 400/401 branch every time.
//   - The local page creates a short-lived session automatically after the
//     phone joins the password-protected ESP access point.
//   - UART writes happen only in loop(). Handlers run in the AsyncTCP task,
//     touching Serial1 from there races the poll loop, so they hand lines to
//     a FreeRTOS queue instead. The MQTT callback runs from mqtt.loop(), which
//     loop() calls, so it shares loop()'s context and needs no lock.
//
// Libraries (Arduino IDE -> Library Manager):
//   - ESPAsyncWebServer + AsyncTCP
//   - ArduinoJson (pulled in by AsyncJson.h)
//   - PubSubClient (Nick O'Leary)
//   mbedTLS and LittleFS ship with the ESP32 core.
// ─────────────────────────────────────────────────────────────────────────────

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_wifi.h>
#include <AsyncTCP.h>
// Must precede ESPAsyncWebServer.h: that header gates its JSON support on
// __has_include("ArduinoJson.h"), and the Arduino builder only puts ArduinoJson
// on the include path if the sketch names it. Without this line
// AsyncCallbackJsonWebHandler silently does not exist.
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <time.h>
#include "LittleFS.h"

#include "config.h"
#include "protocol.h"
#include "urlencode.h"
#include "portal_scrape.h"
#include "nonce.h"

DNSServer dnsServer;
AsyncWebServer server(HTTP_PORT);

#if MQTT_TLS
WiFiClientSecure net;
#else
WiFiClient net;
#endif
PubSubClient mqtt(net);

// Lines waiting to go out on the UART. Written by the AsyncTCP task, read by
// loop(), which is the only context that touches Serial1.
static QueueHandle_t txQueue;

// Last known low-level state, for /api/status. Written by loop(), read by the
// AsyncTCP task, so every access sits in a critical section.
static portMUX_TYPE snapMux = portMUX_INITIALIZER_UNLOCKED;
static int      snapBatt  = -1;          // -1 = not reported yet
static int      snapState = -1;          // -1 = unknown, else SystemState
static char     snapEvent[PROTO_MAX_LINE] = "";
static uint32_t snapEventAt = 0;
static uint32_t uartRejected = 0;

// Session state. Both handlers that touch it run in the AsyncTCP task, so it
// needs no lock — only loop() runs elsewhere and loop() never reads it.
static String   sessionToken = "";
static uint32_t sessionExpiry = 0;
static uint32_t lastStaAttempt = 0;

// Current proximity code. Rotated by loop(), read by the AsyncTCP task through
// /api/proximity-code, so it needs its own critical section. Kept separate from
// snapMux to keep each section as short as possible.
static portMUX_TYPE codeMux = portMUX_INITIALIZER_UNLOCKED;
static char     proximityCode[CODE_LENGTH + 1] = "";
static uint32_t lastCodeRotate = 0;

// Replay protection. The backend stamps every command with a nonce; a repeat
// means either a retry we already served or an attacker replaying a captured
// publish, and both should be ignored.
static uint32_t nonceRing[NONCE_CACHE];
static uint8_t  nonceHead = 0;

// Dispense sequencer. The Uno's protocol is one line at a time with a DONE
// coming back later, so a dispense is three UART lines spread over seconds.
// Blocking here would starve the DNS server and mqtt.loop(), so it is a state
// machine ticked from loop().
enum DispState { DISP_IDLE, DISP_MOVING, DISP_HOLD };
static DispState dispState   = DISP_IDLE;
static uint32_t  dispSince   = 0;
static int       dispSlot    = 0;
// The nonce verbatim, not its fingerprint: mqtt_listener.py stores the whole
// rack/event payload as the audit log's metadata, and correlating a command to
// its outcome means matching this against the nonce key_service.py logged.
static char      dispNonce[NONCE_STR_MAX] = "";
// Set by handleReply() when the Uno reports a completed move, consumed by
// dispenseTick(). A flag rather than a direct call keeps the sequencer below
// the reply handler in the file and avoids a forward declaration.
static bool      dispDoneSeen = false;

// True once the Uno has confirmed it physically reached the home switch.
// Before that, its position counter is just "wherever it was at boot" — SLOT
// moves would be relative to an unverified reference. queueLine("HOME","?")
// is enqueued once in setup(), and startDispense() refuses to move the rack
// until handleReply() sees this flip true, so "slot 4" can never silently
// mean the wrong physical slot.
static bool      lowLevelHomed = false;

// ── Auth ─────────────────────────────────────────────────────────────────────

static bool ctEqual(const uint8_t *a, const uint8_t *b, size_t n) {
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

static String genToken(size_t len = 40) {
  static const char *chars =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  String s;
  s.reserve(len);
  for (size_t i = 0; i < len; i++) s += chars[esp_random() % 62];
  return s;
}

// millis() wraps every ~49.7 days. "millis() > sessionExpiry" compares
// absolute values, so after the wrap a live session's expiry sits far in the
// numeric future and the session never expires — it fails open. The
// subtraction stays correct across the wrap.
static bool sessionLive() {
  return sessionToken.length() && (int32_t)(millis() - sessionExpiry) < 0;
}

static bool checkSession(AsyncWebServerRequest *request) {
  if (!sessionLive()) return false;
  if (!request->hasHeader("X-Session-Token")) return false;
  // ESPAsyncWebServer 3.x returns the value directly; the original's
  // header(...)->value() is a 2.x-only API and no longer compiles.
  const String given = request->header("X-Session-Token");
  // An early return on the first mismatching byte leaks the length of the
  // matching prefix through response timing, which is enough to walk a token
  // out one byte at a time.
  if (given.length() != sessionToken.length()) return false;
  return ctEqual((const uint8_t *)given.c_str(),
                     (const uint8_t *)sessionToken.c_str(), given.length());
}

// ── Outbound protocol lines ──────────────────────────────────────────────────

// Validates through the same parser the low level uses, then queues. Anything
// the low level would answer with ERR is rejected here instead of burning a
// round trip.
static bool queueLine(const char *verb, const char *arg) {
  char line[PROTO_MAX_LINE];
  ProtoCmd c;

  if (!proto_fmt(line, sizeof line, verb, arg)) return false;
  proto_parse(line, &c);
  if (!c.ok) return false;
  switch (c.verb) {
    case PCMD_SLOT: case PCMD_GOTO: case PCMD_ANGLE: case PCMD_ACTUATE:
    case PCMD_HOME: case PCMD_BATT_Q: case PCMD_STATUS_Q:
      break;
    default:
      return false;   // not a command this end is allowed to send
  }
  return xQueueSend(txQueue, line, 0) == pdTRUE;
}

// ── Inbound protocol lines ───────────────────────────────────────────────────

static void noteEvent(const char *line) {
  size_t j = 0;
  char clean[PROTO_MAX_LINE];
  // Drops the trailing '\n' and anything that would need escaping, so the
  // value can be dropped straight into the /api/status JSON.
  for (size_t i = 0; line[i] && j < sizeof clean - 1; i++) {
    const char c = line[i];
    if (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') clean[j++] = c;
  }
  clean[j] = '\0';

  portENTER_CRITICAL(&snapMux);
  memcpy(snapEvent, clean, j + 1);
  snapEventAt = millis();
  portEXIT_CRITICAL(&snapMux);
}

static void handleReply(const char *line) {
  ProtoCmd c;
  proto_parse(line, &c);

  switch (c.verb) {
    case PCMD_BATT_VAL:
      if (c.ok) {
        portENTER_CRITICAL(&snapMux);
        snapBatt = (int)c.num;
        portEXIT_CRITICAL(&snapMux);
      }
      return;
    case PCMD_STATUS_VAL:
      if (c.ok) {
        portENTER_CRITICAL(&snapMux);
        snapState = (int)c.num;
        portEXIT_CRITICAL(&snapMux);
      }
      return;
    case PCMD_ACK:
    case PCMD_DONE:
    case PCMD_ERR:
      noteEvent(line);
      // ponytail: any DONE advances the sequencer. A manual /api/action move
      // running concurrently with a dispense could satisfy the wrong wait; the
      // DISPENSE_TIMEOUT_MS ceiling means the worst case is one early solenoid
      // pulse, not a stuck rack. Tag commands with an id if that ever matters.
      if (c.verb == PCMD_DONE) {
        dispDoneSeen = true;
        if (!strcmp(c.arg, "HOME")) {
          lowLevelHomed = true;
          Serial.println("[home] Uno confirmed home switch reached");
        }
      } else if (c.verb == PCMD_ERR && !strcmp(c.arg, "HOME_SWITCH_NOT_FOUND")) {
        Serial.println("[home] Uno never found the home switch — dispensing stays blocked");
      } else if (c.verb == PCMD_ERR && !strcmp(c.arg, "REBOOT")) {
        // The Uno restarted underneath us. Its step counter is back at zero
        // wherever the rack happens to be standing, so every slot target we
        // would compute from here is measured from a reference that no longer
        // exists — and nothing about that is visible from this end.
        //
        // This is not rare. Opening a serial monitor on the Uno toggles DTR
        // and resets the ATmega, which happens constantly during bench work.
        // Without this the rack keeps dispensing confidently from a dead
        // reference and hands out the wrong key with no error anywhere.
        lowLevelHomed = false;
        dispState = DISP_IDLE;          // abandon any move that was in flight
        queueLine("HOME", "?");
        Serial.println("[home] Uno rebooted — position invalidated, re-homing");
      }
      return;
    default:
      return;
  }
}

// ── Campus captive portal ────────────────────────────────────────────────────
// The uplink network associates freely and then intercepts everything until its
// login form is POSTed, so association is not the same thing as connectivity.
//
// This runs only while MQTT is down. A live MQTT session is itself proof the
// portal session is open, and its keepalives are what hold the session open, so
// in steady state this costs nothing and never blocks loop().

#if PORTAL_LOGIN

static bool portalOk = false;

// True only for a bare 204. A 200 with a body, a redirect to the portal, or a
// timeout all mean traffic is being rewritten.
static bool portalProbe() {
  HTTPClient http;
  http.setConnectTimeout(PORTAL_TIMEOUT_MS);
  http.setTimeout(PORTAL_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(PORTAL_PROBE_URL)) return false;
  const int rc = http.GET();
  http.end();
  return rc == 204;
}

// FortiGate hands out a per-session `magic` token in the login form, so this is
// a two-step exchange: GET the form, scrape the magic, POST it back with the
// credential. A stale or missing magic is rejected exactly as a wrong password
// would be, which is why the scrape has its own host test.
static bool portalLogin() {
  // Pinned trust anchor, not setInsecure(): this request carries the campus
  // credential, and an unverified socket would hand it to anyone who can answer
  // on the portal's port.
  WiFiClientSecure tls;
  tls.setCACert(PORTAL_ROOT_CA);
  tls.setTimeout(PORTAL_TIMEOUT_MS / 1000);

  char magic[PORTAL_MAGIC_MAX];
  {
    HTTPClient http;
    http.setConnectTimeout(PORTAL_TIMEOUT_MS);
    http.setTimeout(PORTAL_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(tls, PORTAL_FORM_URL)) return false;
    const int rc = http.GET();
    if (rc != 200) {
      Serial.printf("[portal] form GET -> %d\n", rc);
      http.end();
      return false;
    }
    if (http.getSize() > PORTAL_FORM_MAX) {
      Serial.printf("[portal] form too large (%d B), refusing\n", http.getSize());
      http.end();
      return false;
    }
    const String form = http.getString();
    http.end();
    if (!scrape_attr(form.c_str(), PORTAL_MAGIC_TAG, magic, sizeof magic)) {
      // Either the portal changed its markup or we are already logged in and it
      // served the "Authentication Successful" page, which carries no magic.
      Serial.println("[portal] no magic in form");
      return false;
    }
  }

  char redir[128], user[128], pass[128];
  url_encode(PORTAL_REDIR, redir, sizeof redir);
  url_encode(PORTAL_USERNAME, user, sizeof user);
  url_encode(PORTAL_PASSWORD, pass, sizeof pass);
  // magic is 16 hex chars from the portal's own markup, but encode it anyway
  // rather than trusting remote bytes to be safe in a form body.
  char magicEnc[PORTAL_MAGIC_MAX * 3];
  url_encode(magic, magicEnc, sizeof magicEnc);

  char body[512];
  const int n = snprintf(body, sizeof body, PORTAL_LOGIN_BODY,
                         redir, magicEnc, user, pass);
  if (n < 0 || (size_t)n >= sizeof body) {
    // Truncated body would POST a cut-off password and read as bad credentials.
    Serial.println("[portal] login body truncated, refusing");
    return false;
  }

  HTTPClient http;
  http.setConnectTimeout(PORTAL_TIMEOUT_MS);
  http.setTimeout(PORTAL_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(tls, PORTAL_LOGIN_URL)) return false;
  http.addHeader("Content-Type", PORTAL_CONTENT_TYPE);
  const int rc = http.POST((uint8_t *)body, (size_t)n);
  const String reply = (rc == 200) ? http.getString() : String();
  http.end();
  memset(body, 0, sizeof body);          // don't leave the password on the stack

  // The request body is deliberately never logged: it holds the credential, and
  // the serial console is not a private channel. FortiGate answers 200 for a
  // rejected login too, so rc is not the verdict — the probe in portalService()
  // is. This only separates "wrong password" from "something else broke", which
  // is the difference between two very different debugging afternoons.
  const bool rejected = reply.indexOf(PORTAL_REJECT_MARKER) >= 0;
  Serial.printf("[portal] login POST -> %d%s\n", rc,
                rejected ? " (credential rejected)" : "");
  return rc > 0 && rc < 400 && !rejected;
}

// ponytail: the HTTP calls block loop() for up to PORTAL_TIMEOUT_MS, which
// stalls dnsServer.processNextRequest() for that long. Only happens while the
// broker is unreachable, and the AsyncTCP web server runs in its own task, so
// the portal page keeps serving. Move to a task if the DNS stall ever shows.
static void portalService() {
  static uint32_t nextTry = 0;
  static bool tried = false;

  if (WiFi.status() != WL_CONNECTED) { portalOk = false; tried = false; return; }
  if (mqtt.connected()) { portalOk = true; return; }
  if (tried && (int32_t)(millis() - nextTry) < 0) return;
  tried = true;

  portalOk = portalProbe();
  if (!portalOk) {
    Serial.println("[portal] traffic intercepted, logging in");
    if (portalLogin()) portalOk = portalProbe();
    Serial.printf("[portal] %s\n", portalOk ? "session open" : "still blocked");
  }
  nextTry = millis() + (portalOk ? PORTAL_RECHECK_MS : PORTAL_RETRY_MS);
}

#else
static const bool portalOk = true;      // nothing to log into
static inline void portalService() {}
#endif

// ── MQTT ─────────────────────────────────────────────────────────────────────
// Everything here runs from loop() (mqtt.loop() dispatches the callback in the
// caller's context), so it shares the UART reader's context and needs no lock
// except where it touches proximityCode, which the web handler also reads.

// TLS certificates carry notBefore/notAfter. The ESP32's clock starts at the
// epoch, so a handshake before the first NTP sync fails validation and surfaces
// only as PubSubClient rc=-2 — hours of "the broker is broken" for a clock bug.
// Lazy and non-blocking because STA association happens in loop(), not setup():
// a blocking sync in setup() would always time out.
static bool timeSynced() {
  static bool requested = false;
  if (!requested) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    requested = true;
  }
  return time(nullptr) >= 1700000000;   // 2023-11-14, comfortably past any cert
}

static void generateCode() {
  static const char *alphabet = CODE_ALPHABET;
  const size_t n = strlen(alphabet);
  portENTER_CRITICAL(&codeMux);
  for (size_t i = 0; i < CODE_LENGTH; i++) proximityCode[i] = alphabet[esp_random() % n];
  proximityCode[CODE_LENGTH] = '\0';
  portEXIT_CRITICAL(&codeMux);
  lastCodeRotate = millis();
}

static void copyCode(char *out) {
  portENTER_CRITICAL(&codeMux);
  memcpy(out, proximityCode, CODE_LENGTH + 1);
  portEXIT_CRITICAL(&codeMux);
}

// Retained: Render's free tier spins the backend down, and a restart drops the
// Redis cache. Retained means the broker replays the current code on subscribe
// instead of leaving every member locked out until the next rotation.
static void publishCode() {
  if (!mqtt.connected()) return;
  char code[CODE_LENGTH + 1];
  copyCode(code);
  char payload[64];
  snprintf(payload, sizeof payload, "{\"code\":\"%s\"}", code);
  mqtt.publish(TOPIC_CODE, payload, true);
  Serial.printf("[code] %s -> %s\n", code, TOPIC_CODE);
}

static bool nonceSeen(uint32_t nonce) {
  for (uint8_t i = 0; i < NONCE_CACHE; i++) if (nonceRing[i] == nonce) return true;
  return false;
}

static void rememberNonce(uint32_t nonce) {
  nonceRing[nonceHead] = nonce;
  nonceHead = (nonceHead + 1) % NONCE_CACHE;
}

// Key is "event", not "status" — mqtt_listener.py reads data.get("event") and
// logs it as slot_<event>. The nonce goes back as the string it arrived as, so
// the audit log can be joined against the command that caused it.
static void publishRackEvent(const char *event, int slot, const char *nonce) {
  if (!mqtt.connected()) return;
  char payload[128];
  snprintf(payload, sizeof payload,
           "{\"event\":\"%s\",\"slot_number\":%d,\"nonce\":\"%s\"}",
           event, slot, nonce);
  mqtt.publish(TOPIC_RACK_EVT, payload);
}

// ── Dispense sequencer ───────────────────────────────────────────────────────

static bool startDispense(int slot, const char *nonce) {
  if (!lowLevelHomed) return false;                   // unverified position — refuse
  if (dispState != DISP_IDLE) return false;          // one at a time
  if (slot < 1 || slot > SLOT_COUNT) return false;

  // Send the slot number, not a position. The Uno owns the gearing and the
  // microstep setting, so it is the only board that can turn "slot 7" into a
  // step target — and the only board that has to be reflashed when the
  // mechanics change.
  char slotArg[8];
  snprintf(slotArg, sizeof slotArg, "%d", slot);
  if (!queueLine("SLOT", slotArg)) return false;

  dispSlot = slot;
  snprintf(dispNonce, sizeof dispNonce, "%s", nonce);
  dispDoneSeen = false;
  dispSince = millis();
  dispState = DISP_MOVING;
  return true;
}

static void dispenseTick() {
  if (dispState == DISP_IDLE) return;

  const uint32_t age = millis() - dispSince;

  if (dispState == DISP_MOVING) {
    if (dispDoneSeen) {
      dispDoneSeen = false;
      queueLine("ACTUATE", "1");
      dispSince = millis();
      dispState = DISP_HOLD;
      return;
    }
    if (age >= DISPENSE_TIMEOUT_MS) {
      publishRackEvent("failed", dispSlot, dispNonce);
      Serial.printf("[rack] slot %d timed out waiting for DONE\n", dispSlot);
      dispState = DISP_IDLE;
    }
    return;
  }

  // DISP_HOLD: release and report in the same step. The Uno drops the solenoid
  // on its own 5 s safety timeout, so a lost ACTUATE:0 is not a stuck latch.
  if (age >= DISPENSE_HOLD_MS) {
    queueLine("ACTUATE", "0");
    publishRackEvent("dispensed", dispSlot, dispNonce);
    Serial.printf("[rack] slot %d dispensed\n", dispSlot);
    dispState = DISP_IDLE;
  }
}

// ── Command intake ───────────────────────────────────────────────────────────

static void onMessage(char *topic, byte *payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) { Serial.println("[mqtt] bad JSON"); return; }

  // The backend sends a 32-hex-char string (secrets.token_hex(16)), not a
  // number. Reading it straight into a uint32_t yielded 0 and dropped every
  // command the website issued — hence nonce_fold(), which keeps the ring
  // 32-bit while accepting the real wire format.
  const char *nonceStr = doc["nonce"].as<const char *>();
  char nonceBuf[NONCE_STR_MAX];
  if (!nonceStr) {                                   // bare number, hand-sent
    snprintf(nonceBuf, sizeof nonceBuf, "%lu", (unsigned long)(doc["nonce"] | 0UL));
    nonceStr = nonceBuf;
  }
  const uint32_t nonce = nonce_fold(nonceStr);
  if (!nonce)            { Serial.println("[mqtt] no nonce, dropped"); return; }
  if (nonceSeen(nonce))  { Serial.printf("[mqtt] replay %s\n", nonceStr); return; }
  rememberNonce(nonce);

  // "unlock" is a key *return*: mqtt_service.py publishes it on the same topic
  // and it needs the exact same platter move, so it shares the sequencer.
  const char *action = doc["action"] | "";
  if (strcmp(action, "dispense") && strcmp(action, "unlock")) {
    Serial.printf("[mqtt] unknown action %s\n", action);
    return;
  }

  const int slot = doc["slot_number"] | doc["slot_id"] | 0;
  if (!startDispense(slot, nonceStr)) {
    publishRackEvent("failed", slot, nonceStr);
    Serial.printf("[mqtt] %s slot %d rejected (%s)\n", action, slot,
                  !lowLevelHomed ? "not homed yet" : "busy or out of range");
    return;
  }
  Serial.printf("[mqtt] %s slot %d\n", action, slot);
  (void)topic;
}

// ── Connection / telemetry ───────────────────────────────────────────────────

static void sendHeartbeat() {
  if (!mqtt.connected()) return;

  char payload[96];
  snprintf(payload, sizeof payload, "{\"firmware_version\":\"%s\"}", FIRMWARE_VERSION);
  mqtt.publish(TOPIC_HEARTBEAT, payload);

  int batt;
  portENTER_CRITICAL(&snapMux);
  batt = snapBatt;
  portEXIT_CRITICAL(&snapMux);

  // Only report a battery number the Uno actually gave us. kms_enclosure
  // hardcodes 100, which is worse than absent: it looks like a healthy reading.
  if (batt >= 0)
    snprintf(payload, sizeof payload, "{\"battery_pct\":%d,\"rssi\":%d}", batt, WiFi.RSSI());
  else
    snprintf(payload, sizeof payload, "{\"rssi\":%d}", WiFi.RSSI());
  mqtt.publish(TOPIC_TELEM, payload);
}

// Non-blocking: called every loop(), returns immediately unless the backoff has
// elapsed. mqtt.connect() itself blocks for the TLS handshake, which is why the
// backoff caps at MQTT_RETRY_MAX_MS instead of retrying every pass.
static void mqttService() {
  static uint32_t nextAttempt = 0;
  static uint32_t backoff = MQTT_RETRY_MIN_MS;

  if (mqtt.connected()) { mqtt.loop(); return; }
  if (WiFi.status() != WL_CONNECTED) return;
  // Before timeSynced(), not after. SNTP fires its first request the moment
  // configTime() is called and then waits CONFIG_LWIP_SNTP_UPDATE_DELAY — 3 hours
  // on this core — before trying again. A request sent into a portal-blocked
  // network is silently dropped, so the clock would not be set for three hours
  // and every TLS handshake until then fails with rc=-2.
  if (!portalOk) return;
  if (nextAttempt && (int32_t)(millis() - nextAttempt) < 0) return;
  if (!timeSynced()) { nextAttempt = millis() + MQTT_RETRY_MIN_MS; return; }

  const String clientId = String("kms-") + DEVICE_UUID;
  if (mqtt.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
    Serial.println("[mqtt] connected");
    mqtt.subscribe(TOPIC_RACK_CMD, 1);
    publishCode();        // re-seed the backend after any reconnect
    sendHeartbeat();
    backoff = MQTT_RETRY_MIN_MS;
  } else {
    Serial.printf("[mqtt] connect failed rc=%d, retry in %lums\n",
                  mqtt.state(), (unsigned long)backoff);
    backoff = backoff * 2 > MQTT_RETRY_MAX_MS ? MQTT_RETRY_MAX_MS : backoff * 2;
  }
  nextAttempt = millis() + backoff;
}

// ── HTTP ─────────────────────────────────────────────────────────────────────

static void sendErr(AsyncWebServerRequest *r, int code, const char *msg) {
  char body[96];
  snprintf(body, sizeof body, "{\"error\":\"%s\"}", msg);
  r->send(code, "application/json", body);
}

static void onPortalConfig(AsyncWebServerRequest *request) {
  char body[192];
  snprintf(body, sizeof body, "{\"website_url\":\"%s\"}", KMS_WEBSITE_URL);
  request->send(200, "application/json", body);
}

static void onLogin(AsyncWebServerRequest *request, JsonVariant &json) {
  (void)json;
  sessionToken = genToken();
  sessionExpiry = millis() + SESSION_TTL_MS;

  char body[80];
  snprintf(body, sizeof body, "{\"token\":\"%s\"}", sessionToken.c_str());
  request->send(200, "application/json", body);
}

static void onAction(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!checkSession(request)) { sendErr(request, 403, "auth"); return; }
  if (!json.is<JsonObject>())  { sendErr(request, 400, "bad_body"); return; }

  bool queued = false;

  // Escape hatch: a literal protocol line. Still parsed and verb-checked by
  // queueLine(), so it cannot smuggle a reply verb or a malformed param.
  const char *raw = json["line"].as<const char *>();
  if (raw && *raw) {
    ProtoCmd c;
    proto_parse(raw, &c);
    switch (c.verb) {
      case PCMD_SLOT:      queued = c.ok && queueLine("SLOT", c.arg);    break;
      case PCMD_GOTO:      queued = c.ok && queueLine("GOTO", c.arg);    break;
      case PCMD_ANGLE:     queued = c.ok && queueLine("ANGLE", c.arg);   break;
      case PCMD_ACTUATE:   queued = c.ok && queueLine("ACTUATE", c.arg); break;
      case PCMD_BATT_Q:    queued = queueLine("BATT", "?");             break;
      case PCMD_STATUS_Q:  queued = queueLine("STATUS", "?");           break;
      default:             sendErr(request, 400, "bad_line");           return;
    }
    if (!queued) { sendErr(request, 400, "bad_line"); return; }
    request->send(202, "application/json", "{\"queued\":true}");
    return;
  }

  const char *cmd = json["cmd"].as<const char *>();
  if (!cmd) { sendErr(request, 400, "no_cmd"); return; }

  if (!strcmp(cmd, "SLOT")) {
    // Bench equivalent of a dispense move, without the solenoid or the nonce.
    if (!json["slot"].is<long>()) { sendErr(request, 400, "bad_slot"); return; }
    const long slot = json["slot"].as<long>();
    if (slot < 1 || slot > SLOT_COUNT) { sendErr(request, 400, "bad_slot"); return; }
    char slotArg[8];
    snprintf(slotArg, sizeof slotArg, "%ld", slot);
    queued = queueLine("SLOT", slotArg);
  } else if (!strcmp(cmd, "GOTO")) {
    if (!json["pos"].is<long>()) { sendErr(request, 400, "bad_pos"); return; }
    char pos[16];
    snprintf(pos, sizeof pos, "%ld", (long)json["pos"].as<long>());
    queued = queueLine("GOTO", pos);
  } else if (!strcmp(cmd, "ANGLE")) {
    if (!json["degrees"].is<long>()) { sendErr(request, 400, "bad_degrees"); return; }
    char degrees[16];
    snprintf(degrees, sizeof degrees, "%ld", (long)json["degrees"].as<long>());
    queued = queueLine("ANGLE", degrees);
  } else if (!strcmp(cmd, "ACTUATE")) {
    if (!json["on"].is<bool>()) { sendErr(request, 400, "bad_on"); return; }
    queued = queueLine("ACTUATE", json["on"].as<bool>() ? "1" : "0");
  } else if (!strcmp(cmd, "BATT")) {
    queued = queueLine("BATT", "?");
  } else if (!strcmp(cmd, "STATUS")) {
    queued = queueLine("STATUS", "?");
  } else {
    sendErr(request, 400, "unknown_cmd");
    return;
  }

  if (!queued) { sendErr(request, 503, "queue_full"); return; }
  // 202, not 200: the line is queued for the UART, and DONE/ERR arrives later
  // on /api/status. The original answered {"status":"sent"} before knowing
  // whether anything moved.
  request->send(202, "application/json", "{\"queued\":true}");
}

static void onStatus(AsyncWebServerRequest *request) {
  if (!checkSession(request)) { sendErr(request, 403, "auth"); return; }

  int batt, st;
  char ev[PROTO_MAX_LINE];
  uint32_t evAt;
  portENTER_CRITICAL(&snapMux);
  batt = snapBatt;
  st   = snapState;
  evAt = snapEventAt;
  memcpy(ev, snapEvent, sizeof ev);
  portEXIT_CRITICAL(&snapMux);

  const char *stName = (st < 0) ? "UNKNOWN" : proto_state_name((SystemState)st);

  char body[192];
  snprintf(body, sizeof body,
           "{\"state\":\"%s\",\"battery_pct\":%d,\"last_event\":\"%s\","
           "\"last_event_age_ms\":%lu,\"uart_rejected\":%lu}",
           stName, batt, ev,
           (unsigned long)(evAt ? millis() - evAt : 0),
           (unsigned long)uartRejected);
  request->send(200, "application/json", body);
}

// Session-gated so a captive-portal probe or a passing network scanner cannot
// harvest a live code without first POSTing /api/login. The real gate is WPA2
// membership of the AP — this just keeps drive-by GETs out.
static void onProximityCode(AsyncWebServerRequest *request) {
  if (!checkSession(request)) { sendErr(request, 403, "auth"); return; }

  char code[CODE_LENGTH + 1];
  copyCode(code);

  char body[96];
  // online says whether this code can actually be redeemed. A code only reaches
  // Redis through publishCode(), which no-ops while MQTT is down, so a portal
  // that shows one regardless sends the member off to the website to be told
  // "proximity code invalid" with no hint the cabinet is what is broken.
  snprintf(body, sizeof body,
           "{\"code\":\"%s\",\"rotates_in_ms\":%lu,\"online\":%d}", code,
           (unsigned long)(CODE_ROTATE_MS - (millis() - lastCodeRotate)),
           mqtt.connected() ? 1 : 0);
  request->send(200, "application/json", body);
}

static void sendPortal(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response =
      request->beginResponse(LittleFS, "/index.html", "text/html");
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "0");
  request->send(response);
}

// ── Setup / loop ─────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial1.begin(COMM_BAUD_RATE, SERIAL_8N1, COMM_RX_PIN, COMM_TX_PIN);
  txQueue = xQueueCreate(8, PROTO_MAX_LINE);

  // First thing sent to the Uno, every boot. Dispensing stays refused (see
  // startDispense()) until handleReply() sees the matching DONE:HOME come
  // back, so a SLOT move can never run against an unverified position.
  queueLine("HOME", "?");
  Serial.println("[home] queued HOME:? — dispensing blocked until the Uno confirms");

  if (!LittleFS.begin()) Serial.println("LittleFS mount failed");

  String chip = String((uint32_t)ESP.getEfuseMac(), HEX);
  String apSsid = String(AP_SSID_PREFIX) + chip.substring(chip.length() - 6);
  // AP is brought up first so the phone can connect even when upstream Wi-Fi
  // is unavailable. STA is maintained independently and never blocks the
  // local portal.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("Started AP %s, IP: %s\n", apSsid.c_str(), apIP.toString().c_str());

#if KMS_STA_CONFIGURED
  // Widen the scan range before the first begin(). Must come after
  // WiFi.mode(), which is what actually initialises esp_wifi.
  esp_wifi_set_country_code(STA_COUNTRY, true);
  WiFi.begin(STA_SSID, STA_PASSWORD);
  lastStaAttempt = millis();
  Serial.printf("Starting upstream Wi-Fi: %s\n", STA_SSID);
#else
  Serial.println("Upstream Wi-Fi is not configured; AP remains local-only.");
#endif

  dnsServer.start(DNS_PORT, "*", apIP);

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // AsyncCallbackJsonWebHandler reassembles a chunked body and parses it
  // before the callback runs. getParam("plain", true) only ever populates for
  // urlencoded form posts, which is why the original JSON API never matched.
  AsyncCallbackJsonWebHandler *loginH =
      new AsyncCallbackJsonWebHandler("/api/login", onLogin);
  loginH->setMethod(HTTP_POST);
  loginH->setMaxContentLength(256);
  server.addHandler(loginH);

  AsyncCallbackJsonWebHandler *actionH =
      new AsyncCallbackJsonWebHandler("/api/action", onAction);
  actionH->setMethod(HTTP_POST);
  actionH->setMaxContentLength(256);
  server.addHandler(actionH);

  server.on("/api/status", HTTP_GET, onStatus);
  server.on("/api/portal-config", HTTP_GET, onPortalConfig);
  server.on("/api/proximity-code", HTTP_GET, onProximityCode);

  // Common captive-portal connectivity checks. Returning the local page for
  // these HTTP probes prompts supported phones to open their sign-in browser.
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/portal", HTTP_GET, sendPortal);

  server.onNotFound([](AsyncWebServerRequest *request) {
    if (request->url().startsWith("/api/")) {
      request->send(404, "application/json", "{\"error\":\"not_found\"}");
      return;
    }
    sendPortal(request);
  });

  server.begin();
  Serial.println("Web server started");

  // MQTT is configured here but never connected here: STA association happens
  // in loop(), so setup() has no network yet. mqttService() does the connecting.
#if MQTT_TLS
  net.setCACert(MQTT_ROOT_CA);
#endif
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  // Default 256 B truncates a TLS-framed publish silently — no error, no packet.
  mqtt.setBufferSize(MQTT_TLS ? 1024 : 512);

  generateCode();
  Serial.printf("[code] initial %s\n", proximityCode);
}

void loop() {
  static uint32_t lastPoll = 0;
  char line[PROTO_MAX_LINE];
  static size_t rxPos = 0;
  static bool rxOverflow = false;

  dnsServer.processNextRequest();

#if KMS_STA_CONFIGURED
  static bool reportedConnected = false;
  if (WiFi.status() == WL_CONNECTED) {
    if (!reportedConnected) {
      Serial.printf("Upstream Wi-Fi connected: %s, RSSI %d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      reportedConnected = true;
    }
  } else {
    if (reportedConnected) {
      Serial.println("Upstream Wi-Fi disconnected; retrying.");
      reportedConnected = false;
    }
    if (millis() - lastStaAttempt >= STA_RETRY_INTERVAL_MS) {
      lastStaAttempt = millis();
      // Say why. A failed association is otherwise completely silent, and the
      // portal login never runs without it — so "no internet" reads as an LDAP
      // problem when it is really status 1 (no AP found) or 4 (auth rejected).
      Serial.printf("[sta] %s not up (status %d), retrying\n", STA_SSID,
                    (int)WiFi.status());
      WiFi.begin(STA_SSID, STA_PASSWORD);
    }
  }
#endif

  // Only context that writes the UART.
  while (xQueueReceive(txQueue, line, 0) == pdTRUE) {
    Serial1.write((const uint8_t *)line, strlen(line));
  }

  while (Serial1.available()) {
    const char c = (char)Serial1.read();
    if (c == '\n') {
      if (!rxOverflow && rxPos > 0) {
        line[rxPos] = '\0';
        ProtoCmd parsed;
        proto_parse(line, &parsed);
        if (parsed.verb == PCMD_UNKNOWN || !parsed.ok) uartRejected++;
        else handleReply(line);
      }
      rxPos = 0;
      rxOverflow = false;
    } else if (!rxOverflow && c != '\r') {
      if (rxPos + 1 < sizeof(line)) line[rxPos++] = c;
      else rxOverflow = true;
    }
  }

  // Ask rather than wait: DONE and ERR arrive unsolicited, but STATUS and BATT
  // only come back when asked, and /api/status has to answer with something.
  if (millis() - lastPoll >= POLL_INTERVAL_MS) {
    lastPoll = millis();
    queueLine("STATUS", "?");
    queueLine("BATT", "?");
  }

  portalService();
  mqttService();
  dispenseTick();

  if (millis() - lastCodeRotate >= CODE_ROTATE_MS) {
    generateCode();
    publishCode();
  }

  static uint32_t lastBeat = 0;
  if (millis() - lastBeat >= HEARTBEAT_MS) {
    lastBeat = millis();
    sendHeartbeat();
  }
}
