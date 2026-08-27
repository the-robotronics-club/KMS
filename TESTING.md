# Bench testing — from bare motor to the website

Work down this list in order. Every stage is provable on its own, and each one
rules out a whole class of fault before the next one can confuse you. Skipping
ahead is how you end up with a rack that doesn't move and five possible reasons.

Nothing here needs the website until stage 6.

---

## Before you power anything

**Never plug or unplug the stepper motor while the driver is powered.**
Breaking a coil's current path with the driver live destroys the output stage.
Motor first, then power. Always.

**The motor supply never touches the Arduino or the ESP32.** 24 V into a 5 V
board is instant, permanent death. The only wires between the driver and the
Uno are PUL+, DIR+ and ground.

**Set a current limit on the bench supply** before connecting anything —
1.5 A is plenty to start. If something is miswired the supply trips instead of
cooking a driver.

---

## Stage 0 — the two numbers we cannot guess

Everything downstream depends on these, and both are physical facts about your
hardware, not choices:

| What | Where to find it | Goes in |
|---|---|---|
| Pulses per revolution | DIP switch table printed on the driver | `MICROSTEP_DIVISOR` in `firmware/KMS_LowLevel_ArduinoUno/rack_geometry.h` |
| Number of key slots | Count them on the disc | `SLOT_COUNT`, same file |

The driver is a **DM542-class "Microstep Driver"**, not a TB6600. Its DIP table
is usually SW1-SW3 for microstepping and SW4-SW6 for current. Read the switches,
find the row in the table, and note the pulses/rev.

The code currently assumes **1600 pulses/rev** (200 full steps × 8 microsteps)
and **24 slots**. If either is wrong, fix it in `rack_geometry.h` and re-run:

```bash
gcc -Wall -Wextra -o /tmp/trg firmware/tools/test_rack_geometry.c && /tmp/trg
```

If you change `SLOT_COUNT`, three places must agree — that header,
`firmware/cabinet_esp/config.h`, and the database CHECK constraint in
`backend/alembic/versions/widen_slot_range_to_24.py`.

---

## Stage 1 — the maths, on your laptop

No hardware. Proves the message format and the slot arithmetic before any of it
can be blamed on wiring.

```bash
gcc -Wall -Wextra -o /tmp/tp  firmware/tools/test_protocol.c      && /tmp/tp
gcc -Wall -Wextra -o /tmp/trg firmware/tools/test_rack_geometry.c && /tmp/trg
gcc -Wall -Wextra -o /tmp/tn  firmware/tools/test_nonce.c         && /tmp/tn
```

All three must print `all checks passed`.

---

## Stage 2 — motor turns at all

**Wiring**

| Driver terminal | Goes to |
|---|---|
| PUL+ | Uno pin 6 |
| DIR+ | Uno pin 7 |
| PUL−, DIR− | Uno GND |
| ENA+, ENA− | leave disconnected (driver stays enabled) |
| A+, A−, B+, B− | the motor's two coils |
| VCC, GND | bench supply, 24 V |

Use a multimeter's continuity setting to find which motor wires are a pair —
the two ends of one coil beep together. Those two go to A+/A−; the other pair
goes to B+/B−. Getting a pair split across A and B makes the motor buzz and
judder instead of turning.

**Then**

1. Motor connected to the driver *first*, supply off.
2. Upload `firmware/KMS_LowLevel_ArduinoUno` to the Uno over USB.
3. Bench supply on, 24 V, current limit 1.5 A.
4. Serial Monitor, **9600 baud**, line ending "Newline".
5. Send `GOTO:200`

Expect `ACK:GOTO`, movement, then `DONE:GOTO`.

| What happens | What it means |
|---|---|
| Nothing at all | Wrong baud, or sketch didn't upload |
| `ACK:GOTO` but no movement | Driver has no power, or PUL/DIR wiring, or ENA is pulled low |
| Buzzing, no rotation | Coil pairs split across A and B — re-pair them |
| Turns, then stalls | Current limit too low on the supply, or driver current DIPs too low |

---

## Stage 3 — calibrate steps per revolution

This is the one measurement that makes everything else correct. **Do not skip
it.**

Mark the disc and the frame with a pen so you can see one full turn. Then:

```
GOTO:0          (park at a known place)
GOTO:1600       (or whatever your driver's pulses/rev is)
```

The disc should turn **exactly one revolution** and land on your mark.

| Result | Your real steps/rev | Fix |
|---|---|---|
| Exactly one turn | 1600 — correct | nothing |
| Two turns | 800 | `MICROSTEP_DIVISOR 4` |
| Half a turn | 3200 | `MICROSTEP_DIVISOR 16` |
| Quarter turn | 6400 | `MICROSTEP_DIVISOR 32` |

Change `MICROSTEP_DIVISOR` in `rack_geometry.h`, re-run the stage 1 test,
re-upload, and repeat until one command equals one clean revolution.

**If there is a gearbox or belt between motor and disc**, the ratio multiplies
in here too. One motor revolution is not one disc revolution, and this test is
what tells you.

---

## Stage 4 — slots line up

```
GOTO:0
SLOT:1     -> should sit at your reference position
SLOT:2     -> exactly one slot further
SLOT:13    -> should be roughly opposite slot 1
SLOT:24    -> one slot short of a full turn, back near the start
```

Also check the guards work — neither should move the motor:

```
SLOT:0     -> ERR:BAD_SLOT
SLOT:99    -> ERR:BAD_SLOT
SLOT:abc   -> ERR:BAD_SLOT
```

If the slots are consistently off by one position, that is the one-line fix in
`rack_geometry.h`:

```c
#define SLOT_TO_INDEX(slot) ((slot) - 1)   // try (slot) if slot 1 is one short
```

If they drift progressively — right at slot 1, slightly off by 6, badly off by
20 — the steps/rev from stage 3 is wrong. Go back.

**There is no home switch.** The Uno counts from wherever it powered up, so
park the rack at slot 1 before every power cycle, or the numbering silently
shifts.

---

## Stage 5 — the two boards talk

Only now add the ESP32.

**Wiring — three wires**

| From | To | Note |
|---|---|---|
| ESP32 GPIO17 (TX) | Uno pin 2 | 3.3 V out, the Uno reads it fine |
| Uno pin 3 (TX) | ESP32 GPIO16 (RX) | **Through a level shifter or divider** |
| ESP32 GND | Uno GND | Not optional |

The Uno drives 5 V and the ESP32's pin is rated 3.3 V. A proper level shifter
is best; a resistor divider (1 kΩ from Uno pin 3 to ESP32 RX, 2 kΩ from ESP32 RX
to ground) works in a pinch.

**Then**: ESP32 serial monitor at **115200**. The ESP32 asks `STATUS:?` and
`BATT:?` on its own every 2 seconds. If you see real answers coming back, the
link works — and you have not touched WiFi or the website yet.

Nothing coming back? Check in this order:

1. Common ground
2. TX→RX crossover (not TX→TX)
3. Both ends at 9600
4. The level shifter is actually passing signal

The Uno still accepts USB commands at the same time, so you can drive it by hand
while the ESP32 is attached to confirm the motor half still works.

---

## Stage 6 — a dispense without the website

With the ESP32 on WiFi and MQTT connected:

```bash
mosquitto_pub -h <host> -p 8883 --capath /etc/ssl/certs \
              -u <user> -P <pass> \
              -t 'device/<uuid>/rack/command' \
              -m '{"action":"dispense","slot_number":7,"nonce":"a3f9c2d1e4b5a6978877665544332211"}'
```

Expect on the ESP32 serial: `[mqtt] dispense slot 7`, the platter moves, then a
`dispensed` event published back.

The nonce is a **string**, not a number — the backend mints it with
`secrets.token_hex(16)`. Send the same nonce twice and the second is correctly
ignored as a replay; change it to test again.

---

## Stage 7 — end to end

Backend running and seeded, frontend up, phone on mobile data. Log in, pass
TOTP, read the proximity code off the cabinet portal, submit it, retrieve a key.

Watch all three at once: the browser, the ESP32 serial at 115200, and the Uno
serial at 9600. A failure at this point is almost always the proximity flag
expiring or MQTT credentials — not the rack, because stages 2-6 already proved
the rack.

---

## Known gaps

- **No home switch.** A Uno reset silently reassigns every slot number.
- **A failed dispense still reads as `retrieved`** in the database, so the
  member gets overdue emails for a key they never received.
- **`unlock_door` has no consumer.** The backend publishes it on session start
  and the cabinet firmware does not subscribe to that topic.
