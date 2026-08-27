#ifndef CONFIG_H
#define CONFIG_H

/*
  High-level (cabinet) ESP32 — SoftAP, captive portal, HTTP API, MQTT uplink.

  Board: ESP32 DevKit V1 / WROOM-32 (Arduino FQBN esp32:esp32:esp32).

  Real credentials go in secrets.h, which is gitignored. Every value below is
  #ifndef-guarded, so secrets.h wins wherever it defines one. Copy
  secrets.h.example to secrets.h to start.
*/

#if __has_include("secrets.h")
#include "secrets.h"
#endif

// --- Access point -----------------------------------------------------------
#define AP_SSID_PREFIX  "ESP-KMS-"
#define DNS_PORT        53
#define HTTP_PORT       80

// The ESP access point is intentionally local-only. The phone must leave this
// network before opening the deployed website. The ESP's station interface
// carries MQTT while the AP keeps serving the portal.
#ifndef KMS_WEBSITE_URL
#define KMS_WEBSITE_URL "https://kms-opal.vercel.app/"
#endif

// Upstream Wi-Fi for the cabinet. AP+STA lets the portal stay available while
// the ESP is connected upstream; it does not provide internet/NAT to phones on
// the AP. Set to 0 only for a bench board with no uplink.
#ifndef KMS_STA_CONFIGURED
#define KMS_STA_CONFIGURED 1
#endif

#if KMS_STA_CONFIGURED
#ifndef STA_SSID
#define STA_SSID           "replace-with-upstream-wifi"
#endif
#ifndef STA_PASSWORD
#define STA_PASSWORD       "replace-with-upstream-password"
#endif
// Regulatory domain for the station scan. The ESP32 defaults to world-safe
// mode, which only *scans* channels 1-11 — so an AP on channel 12 or 13 is
// invisible and WiFi.status() reports WL_NO_SSID_AVAIL forever, exactly as a
// wrong SSID would. India allows 1-13, and the campus APs use 13.
#ifndef STA_COUNTRY
#define STA_COUNTRY        "IN"
#endif
#endif

#define STA_RETRY_INTERVAL_MS 10000UL

// --- Campus captive portal ---------------------------------------------------
// The campus Wi-Fi associates with no 802.1X, then hijacks every request until
// its login form is POSTed. Set PORTAL_LOGIN to 0 for any network that simply
// works after association: home Wi-Fi, a phone hotspot, a 4G MiFi.
//
// The defaults below are the real IIT Mandi portal, captured off the network
// rather than guessed. It is a FortiGate: the login form carries a per-session
// `magic` token that must be scraped from a fresh GET before the POST, so this
// is a two-step exchange and a hardcoded magic will always fail.
//
// SECURITY. The credential below sits in the ESP32's flash, which reads out over
// USB with `esptool.py read_flash` — no soldering and no exploit needed. Anyone
// who opens the cabinet or borrows the board recovers a working campus account,
// and if that account is also SSO then it is also mail and every portal behind
// it. Ask IT for a dedicated service account or an IoT MAC exemption if you can;
// otherwise treat this credential as compromised the moment the board leaves
// your hands, and rotate it when the project ends.
//
// The TLS leg is verified, not blind: PORTAL_ROOT_CA pins the portal's actual
// trust anchor, so the credential is not exposed to a campus-LAN MITM. Do not
// swap this for setInsecure() — an unverified socket hands the account to
// anyone who can answer on port 1003.
#ifndef PORTAL_LOGIN
#define PORTAL_LOGIN 0
#endif

#if PORTAL_LOGIN
// Step 1: GET this and scrape the magic. Every GET returns a different one.
#ifndef PORTAL_FORM_URL
#define PORTAL_FORM_URL      "https://login.iitmandi.ac.in:1003/login?"
#endif
// Step 2: POST the form here. FortiGate posts to the portal root, not to /login.
#ifndef PORTAL_LOGIN_URL
#define PORTAL_LOGIN_URL     "https://login.iitmandi.ac.in:1003/"
#endif
// The hidden 4Tredir field: where the portal sends the browser afterwards. The
// firmware ignores the reply, but FortiGate rejects a POST without it.
#ifndef PORTAL_REDIR
#define PORTAL_REDIR         "https://login.iitmandi.ac.in:1003/portal?"
#endif
// Scrape anchor. The magic is the attribute value immediately following this
// literal in the form HTML:
//   <input type="hidden" name="magic" value="046b8ea5ce7bcf0c">
#ifndef PORTAL_MAGIC_TAG
#define PORTAL_MAGIC_TAG     "name=\"magic\" value=\""
#endif
// printf format taking redir, magic, username, password — in that order. All
// four are percent-encoded before substituting, so keep every value out of the
// format string itself: a pasted pre-encoded value would put a literal '%' in
// here and make snprintf read past its arguments.
#ifndef PORTAL_LOGIN_BODY
#define PORTAL_LOGIN_BODY    "4Tredir=%s&magic=%s&username=%s&password=%s"
#endif
#ifndef PORTAL_CONTENT_TYPE
#define PORTAL_CONTENT_TYPE  "application/x-www-form-urlencoded"
#endif
// FortiGate answers 200 for a rejected login and re-serves the login form, so
// the HTTP status says nothing. This literal appears in that re-served form and
// nowhere on the success page — verified against the live portal with a
// deliberately bad username. Only used to make the serial log say "credential
// rejected" instead of "still blocked"; the probe remains the real verdict, so a
// markup change here costs a vague log line, not a broken login.
#ifndef PORTAL_REJECT_MARKER
#define PORTAL_REJECT_MARKER "LDAP Login"
#endif
#ifndef PORTAL_USERNAME
#define PORTAL_USERNAME      "replace-with-ldap-id"
#endif
#ifndef PORTAL_PASSWORD
#define PORTAL_PASSWORD      "replace-with-ldap-password"
#endif

// Trust anchor for the portal's TLS. Go Daddy Root Certificate Authority - G2,
// self-signed, valid to 2037-12-31. The portal serves *.iitmandi.ac.in through
// "Go Daddy Secure Certificate Authority - G2", which chains to this root.
//
// Verified against the live portal — the chain validates against this cert
// alone, with the system store disabled:
//   openssl s_client -connect login.iitmandi.ac.in:1003 \
//     -servername login.iitmandi.ac.in -CAfile gdroot.pem -no-CAstore -no-CApath
// If the campus ever moves off GoDaddy, that command fails first and tells you
// which root to paste in instead.
#ifndef PORTAL_ROOT_CA
#define PORTAL_ROOT_CA \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDxTCCAq2gAwIBAgIBADANBgkqhkiG9w0BAQsFADCBgzELMAkGA1UEBhMCVVMx\n" \
"EDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxGjAYBgNVBAoT\n" \
"EUdvRGFkZHkuY29tLCBJbmMuMTEwLwYDVQQDEyhHbyBEYWRkeSBSb290IENlcnRp\n" \
"ZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTA5MDkwMTAwMDAwMFoXDTM3MTIzMTIz\n" \
"NTk1OVowgYMxCzAJBgNVBAYTAlVTMRAwDgYDVQQIEwdBcml6b25hMRMwEQYDVQQH\n" \
"EwpTY290dHNkYWxlMRowGAYDVQQKExFHb0RhZGR5LmNvbSwgSW5jLjExMC8GA1UE\n" \
"AxMoR28gRGFkZHkgUm9vdCBDZXJ0aWZpY2F0ZSBBdXRob3JpdHkgLSBHMjCCASIw\n" \
"DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAL9xYgjx+lk09xvJGKP3gElY6SKD\n" \
"E6bFIEMBO4Tx5oVJnyfq9oQbTqC023CYxzIBsQU+B07u9PpPL1kwIuerGVZr4oAH\n" \
"/PMWdYA5UXvl+TW2dE6pjYIT5LY/qQOD+qK+ihVqf94Lw7YZFAXK6sOoBJQ7Rnwy\n" \
"DfMAZiLIjWltNowRGLfTshxgtDj6AozO091GB94KPutdfMh8+7ArU6SSYmlRJQVh\n" \
"GkSBjCypQ5Yj36w6gZoOKcUcqeldHraenjAKOc7xiID7S13MMuyFYkMlNAJWJwGR\n" \
"tDtwKj9useiciAF9n9T521NtYJ2/LOdYq7hfRvzOxBsDPAnrSTFcaUaz4EcCAwEA\n" \
"AaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYE\n" \
"FDqahQcQZyi27/a9BUFuIMGU2g/eMA0GCSqGSIb3DQEBCwUAA4IBAQCZ21151fmX\n" \
"WWcDYfF+OwYxdS2hII5PZYe096acvNjpL9DbWu7PdIxztDhC2gV7+AJ1uP2lsdeu\n" \
"9tfeE8tTEH6KRtGX+rcuKxGrkLAngPnon1rpN5+r5N9ss4UXnT3ZJE95kTXWXwTr\n" \
"gIOrmgIttRD02JDHBHNA7XIloKmf7J6raBKZV8aPEjoJpL1E/QYVN8Gb5DKj7Tjo\n" \
"2GTzLH4U/ALqn83/B2gX2yKQOC16jdFU8WnjXzPKej17CuPKf1855eJ1usV2GDPO\n" \
"LPAvTK33sefOT6jEm0pUBsV/fdUID+Ic/n4XuKxe9tQWskMJDE32p2u0mYRlynqI\n" \
"4uJEvlz36hz1\n" \
"-----END CERTIFICATE-----\n"
#endif
#endif

// Probe target. Must answer a bare 204 with no body on an unfiltered network;
// anything else — a 200, a redirect, a timeout — means the portal is in the way.
// Kept on http so the probe stays cheap: an intercepted https request fails as a
// TLS error rather than a readable redirect.
#ifndef PORTAL_PROBE_URL
#define PORTAL_PROBE_URL  "http://connectivitycheck.gstatic.com/generate_204"
#endif
#define PORTAL_TIMEOUT_MS 8000          // per request; a TLS handshake on this
                                        // chip is slow. Fits uint16_t.
#define PORTAL_MAGIC_MAX  33            // FortiGate sends 16 hex chars; 2x + NUL
#define PORTAL_FORM_MAX   8192          // form page is ~2.8 KB, refuse anything
                                        // wildly larger rather than eat the heap
#define PORTAL_RETRY_MS   15000UL       // re-probe after a failed login
#define PORTAL_RECHECK_MS 60000UL       // ...and after a good one, while MQTT is down

// --- UART to the Arduino Uno low-level controller ----------------------------
// Plain ASCII lines, matching KMS_LowLevel_ArduinoUno.ino.
// ESP32 TX -> Uno pin 2 (SoftwareSerial RX)
// ESP32 RX <- Uno pin 3 (SoftwareSerial TX), through a 5V-to-3.3V level shifter
#define COMM_RX_PIN     16
#define COMM_TX_PIN     17
#define COMM_BAUD_RATE  9600

// How often to ask the low level for STATUS and BATT so /api/status has fresh
// numbers without the UI polling the UART itself.
#define POLL_INTERVAL_MS 2000UL

// Session lifetime for the automatic local session.
#define SESSION_TTL_MS  (30UL * 60UL * 1000UL)

// --- Identity ---------------------------------------------------------------
// MUST match the device row created in Admin -> Devices. The value below is the
// seeded demo device, so a stock checkout works without changes.
#ifndef DEVICE_UUID
#define DEVICE_UUID     "11111111-2222-3333-4444-555555555555"
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "cabinet-1.0.0"
#endif

// --- MQTT broker ------------------------------------------------------------
// HiveMQ Cloud in production. Credentials come from the cluster's Access
// Management tab. Give the board its own credential, not the backend's, so one
// can be revoked without killing both.
//
// Local Mosquitto instead? Set MQTT_TLS to 0, MQTT_PORT to 1883, and use the
// laptop's LAN IP — never "localhost", the ESP32 resolves that to itself.
#ifndef MQTT_HOST
#define MQTT_HOST       "xxxxx.s1.eu.hivemq.cloud"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT       8883
#endif
#ifndef MQTT_USERNAME
#define MQTT_USERNAME   "kms-cabinet"
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD   ""
#endif
#ifndef MQTT_TLS
#define MQTT_TLS        1                     // 0 = plaintext, local dev only
#endif

// Reconnect backoff. Non-blocking: the AP portal and the UART poll never wait
// on the broker.
#define MQTT_RETRY_MIN_MS   5000UL
#define MQTT_RETRY_MAX_MS   60000UL

// Root CA for the broker's certificate chain. HiveMQ Cloud serves Let's Encrypt
// certs, which chain to ISRG Root X1 (self-signed, valid to 2035-06-04).
//
// Pinned rather than setInsecure() because the broker is on the public
// internet: an unverified socket lets a MITM publish `dispense` and empty the
// rack. Verify against your own cluster before flashing a fleet:
//   openssl s_client -connect <host>:8883 -showcerts </dev/null
// If the chain ever changes, replace this block with the new root.
#ifndef MQTT_ROOT_CA
#define MQTT_ROOT_CA \
"-----BEGIN CERTIFICATE-----\n" \
"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\n" \
"-----END CERTIFICATE-----\n"
#endif

// Topics. Leave as-is; the backend subscribes to these exact patterns.
#define TOPIC_CODE      "device/" DEVICE_UUID "/access/proximity_code"
#define TOPIC_RACK_CMD  "device/" DEVICE_UUID "/rack/command"
#define TOPIC_RACK_EVT  "device/" DEVICE_UUID "/rack/event"
#define TOPIC_TELEM     "device/" DEVICE_UUID "/power/telemetry"
#define TOPIC_HEARTBEAT "device/" DEVICE_UUID "/esp32/heartbeat"

// --- Proximity code ---------------------------------------------------------
// Second gate, independent of the authenticator: only a phone standing at the
// cabinet can read this off the portal. Published retained so a backend restart
// re-seeds its Redis cache from the broker instead of waiting for a rotation.
#define CODE_ALPHABET   "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"  // no O/0, I/1/l
#define CODE_LENGTH     6
#define CODE_ROTATE_MS  300000UL        // new code every 5 min. The reader has to
                                        // leave the AP, switch to mobile data,
                                        // log in, pass TOTP and pick a key
                                        // before spending it — 60 s did not
                                        // cover that. Keep the backend's
                                        // proximity_code_ttl_seconds above this.
#define HEARTBEAT_MS    30000UL         // heartbeat + telemetry every 30 s
#define NONCE_CACHE     64              // replay-protection history size

// --- Rack geometry ----------------------------------------------------------
// This end sends "SLOT:<n>" and nothing more. It does NOT know the gearing, the
// microstep setting or how many steps a slot is worth — all of that lives in
// firmware/KMS_LowLevel_ArduinoUno/rack_geometry.h, on the board that actually
// drives the motor. Re-gear the rack and only the Uno gets reflashed.
//
// SLOT_COUNT is duplicated here for one job: rejecting an out-of-range slot
// before it costs a UART round trip. Keep it equal to the Uno's SLOT_COUNT.
// The Uno range-checks again on arrival, so a mismatch is caught, not obeyed.
#define SLOT_COUNT          24

// How long the solenoid stays engaged to drop one key, and how long to wait for
// the Uno's DONE before declaring the dispense failed.
#define DISPENSE_HOLD_MS    800UL
#define DISPENSE_TIMEOUT_MS 8000UL

// --- Secrets ----------------------------------------------------------------
#ifndef KMS_SECRETS_SET
#define KMS_SECRETS_SET 1
#endif

#if !KMS_SECRETS_SET
#error "Set AP_PASSWORD in config.h, then #define KMS_SECRETS_SET 1."
#endif

#ifndef AP_PASSWORD
#define AP_PASSWORD     "kmsesp32"   // WPA2, min 8 chars
#endif


#endif // CONFIG_H
