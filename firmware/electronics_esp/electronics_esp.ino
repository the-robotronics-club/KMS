// ─────────────────────────────────────────────────────────────────────────────
// KMS low-level (electronics) ESP32
//
// Speaks the ASCII protocol in protocol.h, carried inside the encrypted UART
// frame in secure_link.h. Drives the stepper, the solenoid relay and the
// battery tap.
//
// This is the half the original electronics_esp.ino never had: it decrypted a
// frame, printed the payload to the debug console, and stopped at
// "// TODO: parse JSON and actuate electronics here". Nothing moved.
//
// Libraries (Arduino IDE -> Library Manager):
//   - AccelStepper (Mike McCauley)
//   mbedTLS ships with the ESP32 core.
// ─────────────────────────────────────────────────────────────────────────────

#include <AccelStepper.h>

#include "config.h"
#include "protocol.h"
#include "secure_link.h"

AccelStepper stepper(AccelStepper::DRIVER, STEPPER_STEP_PIN, STEPPER_DIR_PIN);
SecureLink uart;

SystemState currentState = STATE_IDLE;

bool     solenoidEngaged = false;
uint32_t solenoidSince   = 0;

int      batteryPct    = 100;
uint32_t lastBattRead  = 0;

// ── Transport ────────────────────────────────────────────────────────────────

// Every outbound line goes through here, so the '\n'-only terminator is
// guaranteed in one place. println() would append "\r\n" and the stray '\r'
// would arrive inside the param, breaking exact compares on the far end.
void reply(const char *verb, const char *arg) {
  char line[PROTO_MAX_LINE];
  const size_t n = proto_fmt(line, sizeof line, verb, arg);
  if (!n) {                      // arg too long to frame — say so, don't truncate
    const size_t f = proto_fmt(line, sizeof line, "ERR", "REPLY_TOO_LONG");
    if (f) uart.send(line, f);
    return;
  }
  uart.send(line, n);
  Serial.print("[tx] ");
  Serial.write((const uint8_t *)line, n);
}

void replyNum(const char *verb, long value) {
  char buf[16];
  snprintf(buf, sizeof buf, "%ld", value);
  reply(verb, buf);
}

// ── Hardware ─────────────────────────────────────────────────────────────────

void setSolenoid(bool engage) {
  digitalWrite(SOLENOID_RELAY_PIN,
               (engage == !!SOLENOID_ACTIVE_HIGH) ? HIGH : LOW);
  solenoidEngaged = engage;
  if (engage) solenoidSince = millis();
}

void updateSolenoid() {
  if (!solenoidEngaged) return;
  if (millis() - solenoidSince < SOLENOID_MAX_MS) return;
  // Drop the coil before it overheats — and tell the high level, which would
  // otherwise keep believing the latch is still held. The original disengaged
  // silently, so the two ends disagreed about the physical state from here on.
  setSolenoid(false);
  reply("ERR", "SOLENOID_TIMEOUT");
}

int readBatteryPct() {
  // analogReadMilliVolts() applies the per-chip eFuse ADC calibration. Raw
  // analogRead() * 3.3 / 4095 ignores it and the ESP32 ADC is visibly
  // non-linear near both rails.
  const float pinV = analogReadMilliVolts(VBAT_PIN) / 1000.0f;
  const float packV = pinV * VBAT_DIVIDER_RATIO;
  const float span = VBAT_FULL_V - VBAT_EMPTY_V;
  if (packV >= VBAT_FULL_V) return 100;
  if (packV <= VBAT_EMPTY_V) return 0;
  return (int)((packV - VBAT_EMPTY_V) / span * 100.0f);
}

void updateBattery() {
  if (millis() - lastBattRead < 500) return;
  lastBattRead = millis();
  batteryPct = readBatteryPct();

  // Latch on the way down, release on the way up past a higher mark. A moving
  // stepper sags the pack by a few percent on its own, so a single threshold
  // would toggle in and out of ERROR every time the motor starts.
  if (currentState != STATE_ERROR && batteryPct <= VBAT_CUTOFF_PCT) {
    currentState = STATE_ERROR;
    stepper.stop();
    setSolenoid(false);
    reply("ERR", "LOW_BATTERY");
  } else if (currentState == STATE_ERROR && batteryPct >= VBAT_RECOVER_PCT) {
    currentState = STATE_IDLE;
  }
}

// ── Protocol ─────────────────────────────────────────────────────────────────

void handleLine(const char *line) {
  ProtoCmd c;
  proto_parse(line, &c);

  switch (c.verb) {
    case PCMD_NONE:
      return;                                  // blank line, not an error

    case PCMD_GOTO:
      // toInt() would have turned "GOTO:abc" into position 0 and homed the
      // axis on a corrupt byte.
      if (!c.ok)                        { reply("ERR", "BAD_PARAM_GOTO"); return; }
      if (currentState == STATE_ERROR)  { reply("ERR", "LOW_BATTERY");    return; }
      // Accepting a second GOTO mid-travel emitted two ACKs and only one DONE,
      // leaving the high level permanently one reply behind.
      if (currentState == STATE_MOVING) { reply("ERR", "BUSY");           return; }
      stepper.moveTo(c.num);
      currentState = STATE_MOVING;
      reply("ACK", "GOTO");
      return;

    case PCMD_ACTUATE:
      if (!c.ok)                       { reply("ERR", "BAD_PARAM_ACTUATE"); return; }
      if (currentState == STATE_ERROR) { reply("ERR", "LOW_BATTERY");       return; }
      reply("ACK", "ACTUATE");
      setSolenoid(c.num == 1);
      reply("DONE", "ACTUATE");        // relay throw is immediate
      return;

    case PCMD_BATT_Q:
      replyNum("BATT", batteryPct);
      return;

    case PCMD_STATUS_Q:
      reply("STATUS", proto_state_name(currentState));
      return;

    default: {
      // Echo the offending verb — not the whole line, which would leak the
      // param back onto the wire — so the high level can correlate the
      // failure with the command it sent.
      char verb[24];
      size_t i = 0;
      while (line[i] && line[i] != ':' && i < sizeof verb - 1) {
        verb[i] = line[i];
        i++;
      }
      verb[i] = '\0';

      char msg[PROTO_MAX_LINE];
      snprintf(msg, sizeof msg, "UNKNOWN_CMD_%s", verb);
      reply("ERR", msg);
      return;
    }
  }
}

// ── Setup / loop ─────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial1.begin(COMM_BAUD_RATE, SERIAL_8N1, COMM_RX_PIN, COMM_TX_PIN);
  uart.begin(&Serial1, SHARED_KEY);

  pinMode(STEPPER_EN_PIN, OUTPUT);
  digitalWrite(STEPPER_EN_PIN, LOW);          // A4988 ~ENABLE active-low
  stepper.setMaxSpeed(STEPPER_MAX_SPEED);
  stepper.setAcceleration(STEPPER_ACCELERATION);

  pinMode(SOLENOID_RELAY_PIN, OUTPUT);
  setSolenoid(false);

  analogSetPinAttenuation(VBAT_PIN, ADC_11db);  // full 0..~3.1 V input range
  batteryPct = readBatteryPct();

  // Print the raw tap so VBAT_DIVIDER_RATIO can be calibrated against a meter.
  Serial.printf("Low-level KMS ready. VBAT tap %u mV -> %d%%\n",
                analogReadMilliVolts(VBAT_PIN), batteryPct);
}

void loop() {
  char line[PROTO_MAX_LINE];

  // Non-blocking. readStringUntil() would park here for the stream timeout
  // (1 s) on a partial line, and AccelStepper::run() must be called far more
  // often than once a second or the motor stalls mid-travel.
  if (uart.poll(line, sizeof line)) handleLine(line);

  stepper.run();
  updateSolenoid();
  updateBattery();

  if (currentState == STATE_MOVING && stepper.distanceToGo() == 0) {
    currentState = STATE_IDLE;
    reply("DONE", "GOTO");
    Serial.printf("[pos] %ld\n", stepper.currentPosition());
  }
}
