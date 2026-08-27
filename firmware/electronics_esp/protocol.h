#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
  Simple ASCII Protocol for High-Low Level ESP32 Communication
  Commands are terminated by a newline '\n'

  High -> Low Commands:
  - SLOT:<n>          -> Move to key slot <n>, 1-based. PREFERRED.
  - GOTO:<position>   -> Move stepper to <position>, in raw steps
  - ANGLE:<degrees>   -> Move stepper to an angle in whole degrees
  - ACTUATE:<1/0>     -> Engage(1) or Disengage(0) the solenoid
  - BATT:?            -> Request battery percentage
  - STATUS:?          -> Request system status

  Low -> High Responses:
  - ACK:<cmd>         -> Command acknowledged and started
  - DONE:<cmd>        -> Command finished successfully
  - ERR:<msg>         -> Error occurred
  - BATT:<%>          -> Battery level response
  - STATUS:<state>    -> Status response (IDLE, MOVING, ERROR)
*/

enum SystemState { STATE_IDLE, STATE_MOVING, STATE_ERROR };

/* ---------------------------------------------------------------------------
   Everything below implements the contract above. The prose leaves four things
   implicit that both ends have to agree on byte-for-byte, so they are pinned
   here rather than reinvented per sketch:

   1. Terminator is a single '\n'. Arduino's println() emits "\r\n", so the
      trailing '\r' rides along inside the param and breaks any exact compare
      on the far end ("IDLE\r" != "IDLE"). Format with proto_fmt(), never
      println().
   2. Params are validated. String::toInt() maps "abc" to 0, which turns a
      corrupt "GOTO:abc" into a valid "travel to home" — a silent wrong move
      on real hardware. proto_strict_long() rejects it instead.
   3. BATT and STATUS appear on BOTH directions of the link ("BATT:?" asks,
      "BATT:87" answers). The param disambiguates; the verb alone cannot.
   4. Lines are bounded. PROTO_MAX_LINE lets both ends use fixed buffers and
      drop malloc from the RX path entirely.
   5. SLOT is the command the rack actually uses. The high level knows which
      key the member asked for; only the low level knows the gearing, the
      microstep setting and therefore how many steps a slot is worth. Sending
      the slot number keeps that knowledge in exactly one place — reflashing
      the Uno after a mechanical change never touches the ESP32. GOTO and
      ANGLE stay for bench work.

   Pure C on purpose — no Arduino types — so firmware/tools/test_protocol.c
   can compile and exercise this parser on the host.
   --------------------------------------------------------------------------- */

#include <stddef.h>
#include <string.h>

#define PROTO_EOL       '\n'
#define PROTO_MAX_LINE  64   /* longest legal line incl. verb, ':' and param */

typedef enum {
  PCMD_NONE = 0,     /* blank line — ignore, not an error                    */
  /* High -> Low */
  PCMD_SLOT,         /* num = target key slot, 1-based                        */
  PCMD_GOTO,         /* num = target position                                */
  PCMD_ANGLE,        /* num = target angle in whole degrees                   */
  PCMD_ACTUATE,      /* num = 1 engage / 0 disengage                         */
  PCMD_BATT_Q,       /* "BATT:?"                                            */
  PCMD_STATUS_Q,     /* "STATUS:?"                                          */
  /* Low -> High */
  PCMD_ACK,          /* arg = echoed command                                 */
  PCMD_DONE,         /* arg = echoed command                                 */
  PCMD_ERR,          /* arg = error message                                  */
  PCMD_BATT_VAL,     /* num = percent 0..100                                 */
  PCMD_STATUS_VAL,   /* num = SystemState                                    */
  PCMD_UNKNOWN       /* verb not in the spec                                 */
} ProtoVerb;

typedef struct {
  ProtoVerb verb;
  long      num;                  /* meaning depends on verb, see above      */
  int       ok;                   /* 0 = param present but malformed         */
  char      arg[PROTO_MAX_LINE];  /* raw param, always NUL-terminated        */
} ProtoCmd;

/* Strict base-10 parse. Returns 1 only if the whole string is a signed
   integer that fits in int32 — no leading/trailing junk, no silent 0.
   Rejects "", "abc", "12x", " 12", "999999999999". */
static inline int proto_strict_long(const char *s, long *out) {
  int neg = 0;
  long v = 0;
  if (!s || !*s) return 0;
  if (*s == '-') { neg = 1; s++; }
  else if (*s == '+') { s++; }
  if (!*s) return 0;
  for (; *s; s++) {
    int d;
    if (*s < '0' || *s > '9') return 0;
    d = *s - '0';
    /* Clamp to int32 — AccelStepper positions are 32-bit. */
    if (v > (2147483647L - d) / 10) return 0;
    v = v * 10 + d;
  }
  *out = neg ? -v : v;
  return 1;
}

static inline int proto_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Parses one line (with or without its terminator) into *out.
   Never writes past out->arg. Lenient on input framing — tolerates a stray
   '\r' or surrounding blanks from a peer that used println() — but strict on
   param content, so a garbled param surfaces as ok == 0 instead of a
   plausible-looking wrong value. */
static inline void proto_parse(const char *line, ProtoCmd *out) {
  const char *colon;
  const char *p;
  size_t vlen, plen, i;

  out->verb = PCMD_NONE;
  out->num  = 0;
  out->ok   = 1;
  out->arg[0] = '\0';
  if (!line) return;

  while (proto_is_space(*line)) line++;

  colon = line;
  while (*colon && *colon != ':') colon++;
  vlen = (size_t)(colon - line);

  /* Param is everything after the first ':', right-trimmed. Absent ':' means
     an empty param, which is how a bare "STATUS" is still answerable. */
  p = (*colon == ':') ? colon + 1 : colon;
  plen = strlen(p);
  while (plen && proto_is_space(p[plen - 1])) plen--;
  if (plen > PROTO_MAX_LINE - 1) plen = PROTO_MAX_LINE - 1;
  for (i = 0; i < plen; i++) out->arg[i] = p[i];
  out->arg[plen] = '\0';

  if (vlen == 0) return;  /* blank or ":foo" — PCMD_NONE */

  if (vlen == 4 && !strncmp(line, "SLOT", 4)) {
    out->verb = PCMD_SLOT;
    /* Lower bound only. The upper bound is SLOT_COUNT, which is a property of
       the rack and lives in the low level's config.h — this parser is shared
       by both ends and must not pretend to know it. */
    out->ok = proto_strict_long(out->arg, &out->num) && out->num >= 1;
  } else if (vlen == 4 && !strncmp(line, "GOTO", 4)) {
    out->verb = PCMD_GOTO;
    out->ok = proto_strict_long(out->arg, &out->num);
  } else if (vlen == 5 && !strncmp(line, "ANGLE", 5)) {
    out->verb = PCMD_ANGLE;
    out->ok = proto_strict_long(out->arg, &out->num);
  } else if (vlen == 7 && !strncmp(line, "ACTUATE", 7)) {
    out->verb = PCMD_ACTUATE;
    out->ok = proto_strict_long(out->arg, &out->num)
              && (out->num == 0 || out->num == 1);
  } else if (vlen == 4 && !strncmp(line, "BATT", 4)) {
    /* "BATT:?" / "BATT" = query; "BATT:87" = the answer coming back. */
    if (out->arg[0] == '\0' || !strcmp(out->arg, "?")) {
      out->verb = PCMD_BATT_Q;
    } else {
      out->verb = PCMD_BATT_VAL;
      out->ok = proto_strict_long(out->arg, &out->num)
                && out->num >= 0 && out->num <= 100;
    }
  } else if (vlen == 6 && !strncmp(line, "STATUS", 6)) {
    if (out->arg[0] == '\0' || !strcmp(out->arg, "?")) {
      out->verb = PCMD_STATUS_Q;
    } else {
      out->verb = PCMD_STATUS_VAL;
      if      (!strcmp(out->arg, "IDLE"))   out->num = STATE_IDLE;
      else if (!strcmp(out->arg, "MOVING")) out->num = STATE_MOVING;
      else if (!strcmp(out->arg, "ERROR"))  out->num = STATE_ERROR;
      else out->ok = 0;
    }
  } else if (vlen == 3 && !strncmp(line, "ACK", 3)) {
    out->verb = PCMD_ACK;
  } else if (vlen == 4 && !strncmp(line, "DONE", 4)) {
    out->verb = PCMD_DONE;
  } else if (vlen == 3 && !strncmp(line, "ERR", 3)) {
    out->verb = PCMD_ERR;
  } else {
    out->verb = PCMD_UNKNOWN;
  }
}

/* Writes "<verb>:<arg>\n" into buf. Returns the byte count, or 0 if it would
   not fit — a truncated protocol line is worse than no line, because the peer
   would parse the fragment as a different command. */
static inline size_t proto_fmt(char *buf, size_t cap,
                               const char *verb, const char *arg) {
  size_t vl, al, need;
  if (!buf || !verb) return 0;
  if (!arg) arg = "";
  vl = strlen(verb);
  al = strlen(arg);
  need = vl + 1 + al + 1;            /* verb ':' arg '\n' */
  if (need + 1 > cap) return 0;      /* +1 for the NUL we also write */
  memcpy(buf, verb, vl);
  buf[vl] = ':';
  memcpy(buf + vl + 1, arg, al);
  buf[vl + 1 + al] = PROTO_EOL;
  buf[need] = '\0';
  return need;
}

static inline const char *proto_state_name(enum SystemState s) {
  return s == STATE_IDLE ? "IDLE" : (s == STATE_MOVING ? "MOVING" : "ERROR");
}

#endif /* PROTOCOL_H */
