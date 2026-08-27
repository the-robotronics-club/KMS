#ifndef CONFIG_H
#define CONFIG_H

/*
  Low-level (electronics) ESP32 — pins and tunables.

  Board: ESP32 DevKit V1 / WROOM-32 (Arduino FQBN esp32:esp32:esp32).

  Why ESP32 and not the ESP8266 the upstream low_level_esp32/ project moved to:
  this end has to open the encrypted UART frame, and the ESP8266 Arduino core
  ships no mbedtls/aes.h. An ESP8266 low level can only be reached with the
  protocol in cleartext, which puts cabinet-opening commands on two exposed
  header pins. So both ends stay ESP32 and both use a hardware UART.

  GPIO 6..11 are wired to the module's internal flash and must never be
  driven. GPIO 12 and 15 are strapping pins (12 selects flash voltage at
  boot). GPIO 34..39 are input-only. Every pin below respects that.
*/

// --- UART to the high-level (cabinet) ESP32 ---------------------------------
// Cross the wires: low RX <- high TX, low TX -> high RX. Common ground.
// 115200 both ends. The upstream ESP8266 revision runs SoftwareSerial at 9600
// while the cabinet transmits at 115200 — that pair decodes nothing at all.
#define COMM_RX_PIN        16
#define COMM_TX_PIN        17
#define COMM_BAUD_RATE     115200

// --- Stepper motor (A4988) --------------------------------------------------
#define STEPPER_STEP_PIN   25
#define STEPPER_DIR_PIN    26
#define STEPPER_EN_PIN     27   // A4988 ~ENABLE is active-low
#define STEPPER_MAX_SPEED     1000.0   // steps/sec
#define STEPPER_ACCELERATION   500.0   // steps/sec^2

// --- Solenoid relay ---------------------------------------------------------
#define SOLENOID_RELAY_PIN 4
#define SOLENOID_ACTIVE_HIGH  1      // 0 for the low-trigger relay boards
#define SOLENOID_MAX_MS    5000UL    // coil duty limit; longer cooks the coil

// --- Battery monitor --------------------------------------------------------
// ADC1 channel (GPIO 32..39). ADC2 is unavailable whenever WiFi is on, so
// ADC1 is the only safe choice even though this board keeps its radio off.
// GPIO34 is input-only, which is exactly right for a divider tap.
#define VBAT_PIN           34

// CALIBRATE THIS. Measure the pack with a meter, read the raw mV the firmware
// prints on the debug serial at boot, and set the ratio to
// pack_volts / pin_volts. The nominal value for equal-leg resistors is 2.0,
// but real resistors are 1% parts and the ESP32 divider tap loads them, so the
// measured ratio is what makes the reported percentage mean anything.
#define VBAT_DIVIDER_RATIO 2.0f

// 3S Li-ion pack. Change both if the chemistry changes.
#define VBAT_FULL_V        12.6f
#define VBAT_EMPTY_V        9.6f

// Below this the pack cannot turn the stepper without browning out the logic
// rail and losing the step count, so the board latches STATE_ERROR and
// refuses GOTO instead of stalling mid-travel. Hysteresis stops it
// oscillating on the sag a moving motor itself causes.
#define VBAT_CUTOFF_PCT     10
#define VBAT_RECOVER_PCT    15

// --- Shared UART key --------------------------------------------------------
// 32 bytes, unique per device. secure_link.h derives separate encrypt and MAC
// subkeys from it, so this value is the only secret to manage.
//
// Generate:  openssl rand -hex 32 | sed 's/../0x&,/g'
//
// The placeholder below is the one committed to the public upstream repo. Any
// device flashed with it has no confidentiality and no authentication,
// because the key is in everyone's git history. Replace it, then define
// KMS_KEY_SET to build. Keep the real key OUT of git.
//
// -DKMS_KEY_SET=1 on the build command line also satisfies the guard, which is
// how CI compile-checks this sketch without a real key in the tree.
#ifndef KMS_KEY_SET
#define KMS_KEY_SET 0
#endif

#if !KMS_KEY_SET
#error "Set a unique SHARED_KEY in config.h, then #define KMS_KEY_SET 1. The committed placeholder key is public."
#endif

static const uint8_t SHARED_KEY[32] = {
  0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,
  0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
  0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe,
  0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x11,0x22
};

#endif // CONFIG_H
