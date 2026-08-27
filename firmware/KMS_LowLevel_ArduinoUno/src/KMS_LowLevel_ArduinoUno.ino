/*
 * ==========================================================================
 *  KMS Low-Level Controller - Arduino Uno Port
 * ==========================================================================
 *  Ported from the PlatformIO ESP8266 project (low_level_esp32).
 *  All modules (Config, Protocol, StepperController, SolenoidController,
 *  BatteryMonitor, main) are consolidated into this single .ino file.
 *
 *  Required Library:  AccelStepper by Mike McCauley
 *    Install via Arduino IDE -> Sketch -> Include Library -> Manage Libraries
 *
 *  Board: Arduino Uno (ATmega328P)
 * ==========================================================================
 */

#include <AccelStepper.h>
#include <SoftwareSerial.h>

// =====================================================================
//  CONFIGURATION  (was Config.h)
// =====================================================================

// --- UART Communication (SoftwareSerial) ---
//     On Uno, avoid pins 0/1 (hardware serial used for USB debug).
#define COMM_RX_PIN 2 // SoftwareSerial RX  (connect to High-Level TX)
#define COMM_TX_PIN 3 // SoftwareSerial TX  (connect to High-Level RX)
#define COMM_BAUD_RATE 9600

// --- Stepper Motor (TB6600, common ground wiring) ---
//     Wiring: PUL-/DIR- -> GND, PUL+/DIR+ -> GPIOs
//     Signals are ACTIVE-HIGH (GPIO HIGH triggers optocoupler)
#define STEPPER_STEP_PIN 6          // -> PUL+
#define STEPPER_DIR_PIN 7           // -> DIR+
#define STEPPER_PINS_INVERTED false // common ground = normal logic
#define STEPPER_MAX_SPEED 4000.0    // steps/sec (AccelStepper AVR limit ~4000)
#define STEPPER_ACCELERATION 500.0  // steps/sec^2

// --- Microstepping and rack geometry ---
//     Both live in rack_geometry.h, which is plain C so the slot -> step
//     arithmetic can be tested on a laptop (firmware/tools/test_rack_geometry.c)
//     rather than only on a real rack. Change SLOT_COUNT, MICROSTEP_DIVISOR
//     and the home offset there — this board is the only place in the whole
//     system that knows how many steps a slot is worth.
#include "rack_geometry.h"

// --- Home Switch (rack reference position) ---
//     Two wires, no external parts: D5 to the switch's NO terminal, the
//     switch's COM terminal to GND. INPUT_PULLUP holds the pin HIGH while the
//     switch is open; closing it to ground pulls the pin LOW, hence
//     HOME_SWITCH_ACTIVE_STATE below.
//
//     Use the normally-OPEN contact. On a normally-closed one the sense
//     inverts, so the pin reads "at home" everywhere except the one place it
//     actually is, and homing stops immediately at the wrong position.
//
//     Mount the switch on the fixed frame and the trigger — a screw head or a
//     tab — on the disc. The trigger does NOT need to line up with slot 1;
//     HOME_OFFSET_SLOTS in rack_geometry.h exists to absorb that mismatch.
//
//     Check polarity with "SWITCH:?" before ever running "HOME:?": press the
//     switch by hand and the reported value must flip. If it reads inverted,
//     move the wire to NO, or flip HOME_SWITCH_ACTIVE_STATE to HIGH.
//     Set HOME_SWITCH_FITTED to 0 if the rack has no switch yet. HOME:? then
//     adopts the current position as the reference and answers DONE:HOME
//     immediately, which is what the ESP32 waits for before it will dispense
//     at all. Nothing is measured in that mode — park the rack at slot 1 by
//     hand before power-up, and expect the numbering to drift after any reset.
//     Set it back to 1 the moment a switch exists.
#define HOME_SWITCH_FITTED 0
#define HOME_SWITCH_PIN 5
#define HOME_SWITCH_ACTIVE_STATE LOW
#define HOME_SEEK_SPEED 800.0 // steps/sec, slower than normal moves for a clean stop

// --- Solenoid Relay ---
#define SOLENOID_RELAY_PIN 4 // Digital pin for relay module

// --- Battery Monitor ---
#define VBAT_PIN A0
#define VBAT_DIVIDER_RATIO 2.0 // Adjust for your resistor divider
#define ADC_REF_VOLTAGE 5.0    // Uno uses 5V reference (was 3.3V on ESP)

// =====================================================================
//  PROTOCOL  (was Protocol.h)
// =====================================================================
/*
  Simple ASCII Protocol for High <-> Low Level Communication
  Commands terminated by newline '\n'

  High -> Low Commands:
    SLOT:<n>           Move to key slot <n>, 1-based. This is what the ESP32
                       sends in normal operation; the step maths lives here.
    GOTO:<position>    Move stepper to <position> (in steps)
    ANGLE:<degrees>    Move stepper to <degrees> (converted to steps)
    ACTUATE:<1/0>      Engage(1) or Disengage(0) the solenoid
    HOME:?             Seek the home switch and zero position at slot 1
    SWITCH:?           Raw home-switch pin read (bench-check wiring/polarity)
    BATT:?             Request battery percentage
    STATUS:?           Request system status

  Low -> High Responses:
    ACK:<cmd>          Command acknowledged and started
    DONE:<cmd>         Command finished successfully
    ERR:<msg>          Error occurred
    BATT:<%>           Battery level response
    STATUS:<state>     Status response (IDLE, MOVING, ERROR)
*/

enum SystemState { STATE_IDLE, STATE_MOVING, STATE_ERROR, STATE_HOMING };

// =====================================================================
//  STEPPER CONTROLLER CLASS  (was StepperController.h/.cpp)
// =====================================================================

class StepperController {
public:
  StepperController()
      : motor(AccelStepper::DRIVER, STEPPER_STEP_PIN, STEPPER_DIR_PIN) {}

  void begin() {
    // TB6600 common ground wiring: signals are active-HIGH
    motor.setPinsInverted(STEPPER_PINS_INVERTED, // direction inverted
                          STEPPER_PINS_INVERTED  // step inverted
    );

    // Configure speed and acceleration
    motor.setMaxSpeed(STEPPER_MAX_SPEED);
    motor.setAcceleration(STEPPER_ACCELERATION);

    // Set minimum pulse width for TB6600 (needs >=2.5us)
    motor.setMinPulseWidth(5);

    pinMode(HOME_SWITCH_PIN, INPUT_PULLUP);
  }

  // True while the home switch is physically pressed.
  bool atHomeSwitch() {
    return digitalRead(HOME_SWITCH_PIN) == HOME_SWITCH_ACTIVE_STATE;
  }

  // Begin seeking the home switch: crawl backward (negative direction) at a
  // reduced speed until the switch triggers. Call updateHoming() each loop
  // iteration to know when it has arrived (or failed to).
  void startHoming() {
    savedMaxSpeed = motor.maxSpeed();
    motor.setMaxSpeed(HOME_SEEK_SPEED);
    homingStartPosition = motor.currentPosition();
    // The switch sits somewhere within one revolution of any starting point,
    // so one revolution plus a small margin is the whole search space. This
    // used to move a full 2 revolutions unconditionally and only stopped
    // when the switch tripped — if it never tripped (bad wiring, wrong
    // polarity), the rack silently spun two full turns instead of erroring.
    motor.moveTo(homingStartPosition - (long)(STEPS_PER_REV * 1.1));
  }

  // Call every loop while homing is in progress. Returns true once the
  // switch has been reached and the position has been zeroed to slot 1.
  // Sets homingFailed if the search distance is exhausted without a trigger.
  bool updateHoming() {
    if (atHomeSwitch()) {
      motor.moveTo(motor.currentPosition()); // cancel remaining travel
      motor.setCurrentPosition(0);
      motor.setMaxSpeed(savedMaxSpeed);
      homingFailed = false;
      return true;
    }
    if (motor.distanceToGo() == 0) {
      // Ran out of travel without ever seeing the switch — stop here rather
      // than reporting success at some arbitrary position.
      motor.setMaxSpeed(savedMaxSpeed);
      homingFailed = true;
      return true;
    }
    motor.run();
    return false;
  }

  bool didHomingFail() { return homingFailed; }

  // Declare the current physical position to be `position`, without moving.
  // Used by HOME:? on a rack with no switch fitted, where "home" can only mean
  // "wherever you parked it".
  void setCurrentPosition(long position) {
    motor.moveTo(motor.currentPosition());  // cancel any pending travel first
    motor.setCurrentPosition(position);
  }

  void moveTo(long targetPosition) { motor.moveTo(targetPosition); }

  // Move to a target angle (degrees) - converted using STEPS_PER_REV
  void moveToAngle(float degrees) {
    long steps = angleToSteps(degrees);
    motor.moveTo(steps);
  }

  // Move to key slot `slot` (1-based). Absolute, like every other move here.
  void moveToSlot(int slot) { motor.moveTo(slotToSteps(slot)); }

  // The arithmetic itself is in rack_geometry.h so the host test can reach it.
  static long slotToSteps(int slot) { return rack_slot_to_steps(slot); }

  // Convert degrees to step count based on TB6600 microstepping config
  static long angleToSteps(float degrees) {
    return (long)((degrees / 360.0) * STEPS_PER_REV);
  }

  // Convert step count back to degrees
  static float stepsToAngle(long steps) {
    return (steps * 360.0) / STEPS_PER_REV;
  }

  // Must be called in the main loop - drives the AccelStepper state machine
  void update() { motor.run(); }

  // True while the motor is still traveling to the target
  bool isMoving() { return motor.distanceToGo() != 0; }

  // Returns the current step-counted position
  long getPosition() { return motor.currentPosition(); }

  // Returns the current position in degrees
  float getAngle() { return stepsToAngle(motor.currentPosition()); }

private:
  AccelStepper motor;
  float savedMaxSpeed = STEPPER_MAX_SPEED;
  long homingStartPosition = 0;
  bool homingFailed = false;
};

// =====================================================================
//  SOLENOID CONTROLLER CLASS  (was SolenoidController.h/.cpp)
// =====================================================================

class SolenoidController {
public:
  SolenoidController() : isEngaged(false), engagedTime(0) {}

  void begin() {
    pinMode(SOLENOID_RELAY_PIN, OUTPUT);
    digitalWrite(SOLENOID_RELAY_PIN, LOW); // LOW = relay off
  }

  void actuate(bool engage) {
    isEngaged = engage;
    if (engage) {
      digitalWrite(SOLENOID_RELAY_PIN, HIGH);
      engagedTime = millis();
    } else {
      digitalWrite(SOLENOID_RELAY_PIN, LOW);
    }
  }

  // Checks the safety timeout - disengage if held too long
  void update() {
    if (isEngaged && (millis() - engagedTime > maxEngageDuration)) {
      actuate(false); // Safety timeout reached
    }
  }

private:
  bool isEngaged;
  unsigned long engagedTime;
  static const unsigned long maxEngageDuration = 5000; // 5 seconds max
};

// =====================================================================
//  BATTERY MONITOR CLASS  (was BatteryMonitor.h/.cpp)
// =====================================================================

class BatteryMonitor {
public:
  BatteryMonitor() {}

  void begin() { pinMode(VBAT_PIN, INPUT); }

  // Returns the battery voltage (compensated for voltage divider)
  float getVoltage() {
    int raw = analogRead(VBAT_PIN); // 10-bit ADC, 0-1023

    // Uno ADC reference = 5V (ESP8266 was 3.3V)
    float pinVoltage = (raw / 1023.0) * ADC_REF_VOLTAGE;

    // Compensate for external resistor divider
    float vBat = pinVoltage * VBAT_DIVIDER_RATIO;
    return vBat;
  }

  // Returns estimated percentage (0-100) for 3S LiPo (12.6V max, 9.6V min)
  int getPercentage() {
    float voltage = getVoltage();

    float maxV = 12.6;
    float minV = 9.6;

    if (voltage >= maxV)
      return 100;
    if (voltage <= minV)
      return 0;

    int percent = (int)(((voltage - minV) / (maxV - minV)) * 100);
    return percent;
  }
};

// =====================================================================
//  GLOBALS  &  INSTANCES
// =====================================================================

// ---- VERBOSE LOGGING ----
// Controls how chatty the USB serial log is. It does NOT control where
// commands are read from — see loop(), which always listens to both.
//
// This used to be DEBUG_VIA_USB, a flag that *switched* the input source. It
// shipped set to `true`, which meant the sketch read the USB port and never
// commSerial: every command the ESP32 sent was discarded, silently, with no
// error on either side. The dispense path was dead and the only symptom was
// "the ESP isn't talking to the Arduino".
//
// Listening to both ports costs two lines in loop() and removes that entire
// failure mode, so the flag no longer gates anything that can break the link.
#define VERBOSE_LOG true

SoftwareSerial commSerial(COMM_RX_PIN, COMM_TX_PIN);

StepperController stepper;
SolenoidController solenoid;
BatteryMonitor battery;

SystemState currentState = STATE_IDLE;

// Which move command is currently running, so DONE can echo the right verb.
// It used to answer "DONE:GOTO" for every move, including ANGLE ones, which
// makes a serial log impossible to follow when two commands are in flight.
String lastMoveCmd = "GOTO";

// =====================================================================
//  HELPER: Send response to commSerial (and mirror to the USB log)
// =====================================================================

void sendResponse(const String &msg) {
  // print + '\n', NOT println(): println() emits "\r\n" on Arduino, and the
  // stray '\r' arrives on the far end inside the parameter — "IDLE\r" then
  // compares unequal to "IDLE". The ESP32 parser happens to trim it, but the
  // protocol says one '\n' and there is no reason to lean on that mercy.
  commSerial.print(msg);
  commSerial.print('\n');
  if (VERBOSE_LOG) {
    Serial.print("[RESP] ");
    Serial.println(msg);
  }
}

// =====================================================================
//  COMMAND PROCESSOR
// =====================================================================

// Accumulates bytes from a Stream into a line buffer until '\n', with no
// timeout — a command may arrive one keystroke at a time, arbitrarily slowly
// (e.g. a human typing into the Serial Monitor), and must still be processed
// as a whole line rather than split wherever a pause happened to land.
struct LineBuffer {
  String data;
  void feed(Stream &port, void (*onLine)(String)) {
    while (port.available()) {
      char c = (char)port.read();
      if (c == '\n') {
        onLine(data);
        data = "";
      } else if (c != '\r') {
        if (data.length() < 63) data += c;
      }
    }
  }
};

LineBuffer commLineBuffer;
LineBuffer usbLineBuffer;

void processCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0)
    return;

  if (VERBOSE_LOG) {
    Serial.print("[CMD] ");
    Serial.println(cmd);
  }

  int separatorIndex = cmd.indexOf(':');
  String command = cmd;
  String param = "";

  if (separatorIndex != -1) {
    command = cmd.substring(0, separatorIndex);
    param = cmd.substring(separatorIndex + 1);
  }
  command.toUpperCase();

  if (command == "SLOT") {
    // param.toInt() maps anything non-numeric to 0, so an out-of-range check
    // is also the guard against a garbled line arriving as "go to slot 0".
    long slot = param.toInt();
    if (!rack_slot_valid(slot)) {
      sendResponse("ERR:BAD_SLOT");
      Serial.print("Rejected slot: ");
      Serial.println(param);
      return;
    }
    lastMoveCmd = "SLOT";
    stepper.moveToSlot((int)slot);
    currentState = STATE_MOVING;
    sendResponse("ACK:SLOT");
    Serial.print("Moving to slot ");
    Serial.print(slot);
    Serial.print(" (step ");
    Serial.print(StepperController::slotToSteps((int)slot));
    Serial.println(")");

  } else if (command == "GOTO") {
    long target = param.toInt();
    lastMoveCmd = "GOTO";
    stepper.moveTo(target);
    currentState = STATE_MOVING;
    sendResponse("ACK:GOTO");
    Serial.print("Moving to step: ");
    Serial.println(target);

  } else if (command == "ANGLE") {
    float degrees = param.toFloat();
    lastMoveCmd = "ANGLE";
    stepper.moveToAngle(degrees);
    currentState = STATE_MOVING;
    sendResponse("ACK:ANGLE");
    Serial.print("Moving to angle: ");
    Serial.print(degrees);
    Serial.print(" deg (step ");
    Serial.print(StepperController::angleToSteps(degrees));
    Serial.println(")");

  } else if (command == "HOME") {
#if !HOME_SWITCH_FITTED
    // No switch on this rack. Adopt the current position as the reference and
    // answer DONE:HOME so the ESP32's "refuse to dispense until homed" gate
    // opens. This is a bench convenience, not homing: nothing has been
    // measured, so slot numbering is only as correct as wherever you parked
    // the rack by hand.
    stepper.setCurrentPosition(0);
    currentState = STATE_IDLE;
    sendResponse("DONE:HOME");
    Serial.println("HOME: no switch fitted — adopted current position as slot 1.");
    Serial.println("WARNING: slot numbering is unverified. Park at slot 1 before power-up.");
    return;
#else
    if (stepper.atHomeSwitch()) {
      // Already sitting on the switch: zero immediately, nothing to seek.
      stepper.startHoming();
      stepper.updateHoming();
      currentState = STATE_IDLE;
      sendResponse("DONE:HOME");
    } else {
      stepper.startHoming();
      currentState = STATE_HOMING;
      sendResponse("ACK:HOME");
    }
    Serial.println("Homing to slot 1 switch...");
#endif

  } else if (command == "ACTUATE") {
    bool engage = (param.toInt() == 1);
    solenoid.actuate(engage);
    sendResponse("ACK:ACTUATE");
    Serial.print("Solenoid Actuated: ");
    Serial.println(engage);

  } else if (command == "SWITCH") {
    // Raw home-switch read, independent of homing logic — for bench-checking
    // wiring/polarity before trusting HOME:?. Press the switch by hand and
    // watch this value change; if it never does, the fault is electrical
    // (wiring or HOME_SWITCH_ACTIVE_STATE), not the homing routine.
    bool pressed = stepper.atHomeSwitch();
    sendResponse("SWITCH:" + String(pressed ? 1 : 0));
    Serial.print("Home switch raw state — pressed: ");
    Serial.println(pressed);

  } else if (command == "BATT") {
    int percent = battery.getPercentage();
    sendResponse("BATT:" + String(percent));

  } else if (command == "STATUS") {
    String stateStr;
    if (currentState == STATE_IDLE)
      stateStr = "IDLE";
    else if (currentState == STATE_MOVING)
      stateStr = "MOVING";
    else if (currentState == STATE_HOMING)
      stateStr = "HOMING";
    else
      stateStr = "ERROR";
    sendResponse("STATUS:" + stateStr);

  } else {
    sendResponse("ERR:UNKNOWN_CMD_" + command);
  }
}

// =====================================================================
//  SETUP
// =====================================================================

void setup() {
  // USB debug serial
  Serial.begin(9600);

  // Communication with High-Level MCU
  commSerial.begin(COMM_BAUD_RATE);

  // Initialize hardware controllers
  stepper.begin();
  solenoid.begin();
  battery.begin();

  // Tell the ESP32 we restarted, on the link it actually listens to.
  //
  // The step counter is now zero wherever the rack physically stands, so any
  // slot position the ESP32 still believes in is measured from a reference
  // that no longer exists. It has no other way to notice: this board can
  // reboot on its own (DTR toggle when a serial monitor opens, a brownout
  // while the motor draws current, the reset button) while the ESP32 stays up
  // the whole time and keeps thinking the rack is homed.
  //
  // ERR is already a Low->High verb, so this needs no new protocol.
  sendResponse("ERR:REBOOT");

  Serial.println("Low-Level Arduino Uno KMS Controller Initialized.");
  if (VERBOSE_LOG) {
    Serial.println("Listening on BOTH the ESP32 link and this USB monitor.");
    Serial.println("Try: SLOT:7, GOTO:100, ANGLE:90, ACTUATE:1, HOME:?, SWITCH:?, BATT:?, STATUS:?");
    Serial.print("Slots: ");
    Serial.print(SLOT_COUNT);
    Serial.print("   Steps/rev: ");
    Serial.print(STEPS_PER_REV);
    Serial.print("   Steps/slot: ");
    Serial.println((float)STEPS_PER_REV / SLOT_COUNT);
    // Position is step-counted from wherever the board powered up. A home
    // switch at slot 1 exists now (HOME_SWITCH_PIN), but it is only consulted
    // when HOME:? is sent — send it once after every power-up before trusting
    // SLOT moves, or a stale in-memory position will hand out the wrong key.
    Serial.println("Send HOME:? once at startup to zero position at the slot 1 switch.");
  }
}

// =====================================================================
//  LOOP
// =====================================================================

void loop() {
  // 1. Process incoming commands — from BOTH ports, always.
  //
  //    The ESP32 link is the one that matters in service; the USB monitor is
  //    how you bench-test without an ESP32 attached. There is no flag to get
  //    wrong: type "SLOT:7" into the Serial Monitor and it works, and the
  //    ESP32 sending the same line works at the same time.
  commLineBuffer.feed(commSerial, processCommand);
  usbLineBuffer.feed(Serial, processCommand);

  // 2. Update hardware controllers (non-blocking)
  if (currentState == STATE_HOMING) {
    if (stepper.updateHoming()) {
      currentState = stepper.didHomingFail() ? STATE_ERROR : STATE_IDLE;
      if (stepper.didHomingFail()) {
        sendResponse("ERR:HOME_SWITCH_NOT_FOUND");
        Serial.println("Homing failed: switch never triggered within one revolution.");
      } else {
        sendResponse("DONE:HOME");
        Serial.println("Home switch reached, position zeroed.");
      }
    }
  } else {
    stepper.update();
  }
  solenoid.update();

  // 3. State management - detect when stepper finishes.
  //    AccelStepper's distanceToGo() reaching zero IS the "target reached"
  //    feedback; this is what turns into the dispense confirmation the
  //    website eventually shows.
  if (currentState == STATE_MOVING && !stepper.isMoving()) {
    currentState = STATE_IDLE;
    sendResponse("DONE:" + lastMoveCmd);
    Serial.print("Reached target position: ");
    Serial.println(stepper.getPosition());
  }
}
