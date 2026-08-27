/* Host self-check for protocol.h.
 *
 * The parser is the one piece of the firmware with real branching, and it
 * decides whether "GOTO:abc" moves a physical axis. It is pure C for exactly
 * this reason — no board needed to prove it.
 *
 *   cc -Wall -Wextra -o /tmp/test_protocol firmware/tools/test_protocol.c \
 *      -I firmware/electronics_esp && /tmp/test_protocol
 *
 * ponytail: one runnable check, no framework.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../cabinet_esp/protocol.h"

static ProtoCmd P(const char *line) {
  ProtoCmd c;
  proto_parse(line, &c);
  return c;
}

int main(void) {
  long v;
  ProtoCmd c;
  char buf[PROTO_MAX_LINE];
  char small[8];
  size_t n;

  /* --- strict integer parse -------------------------------------------- */
  assert(proto_strict_long("0", &v) && v == 0);
  assert(proto_strict_long("1200", &v) && v == 1200);
  assert(proto_strict_long("-1200", &v) && v == -1200);
  assert(proto_strict_long("+7", &v) && v == 7);
  assert(proto_strict_long("2147483647", &v) && v == 2147483647L);

  assert(!proto_strict_long("", &v));            /* String::toInt() -> 0 */
  assert(!proto_strict_long("abc", &v));         /* String::toInt() -> 0 */
  assert(!proto_strict_long("12x", &v));         /* String::toInt() -> 12 */
  assert(!proto_strict_long(" 12", &v));
  assert(!proto_strict_long("-", &v));
  assert(!proto_strict_long("2147483648", &v));  /* overflows int32 */
  assert(!proto_strict_long("99999999999999", &v));
  assert(!proto_strict_long(NULL, &v));

  /* --- SLOT -------------------------------------------------------------- */
  c = P("SLOT:1");
  assert(c.verb == PCMD_SLOT && c.ok && c.num == 1);
  c = P("SLOT:24");
  assert(c.verb == PCMD_SLOT && c.ok && c.num == 24);

  /* Slots are 1-based, so 0 and negatives are malformed rather than "slot
     zero". A silent 0 would send the platter home and hand out the wrong
     key — the same class of bug proto_strict_long() exists to prevent. */
  c = P("SLOT:0");
  assert(c.verb == PCMD_SLOT && !c.ok);
  c = P("SLOT:-3");
  assert(c.verb == PCMD_SLOT && !c.ok);
  c = P("SLOT:abc");
  assert(c.verb == PCMD_SLOT && !c.ok);
  c = P("SLOT:");
  assert(c.verb == PCMD_SLOT && !c.ok);

  /* The upper bound belongs to the sketch, not the parser: it depends on
     SLOT_COUNT, which this shared header deliberately does not know. */
  c = P("SLOT:9999");
  assert(c.verb == PCMD_SLOT && c.ok && c.num == 9999);

  /* --- GOTO ------------------------------------------------------------- */
  c = P("GOTO:1200");
  assert(c.verb == PCMD_GOTO && c.ok && c.num == 1200);

  c = P("GOTO:-40");
  assert(c.verb == PCMD_GOTO && c.ok && c.num == -40);

  /* The whole reason for proto_strict_long: this must NOT arrive as
     "travel to position 0". */
  c = P("GOTO:abc");
  assert(c.verb == PCMD_GOTO && !c.ok);
  c = P("GOTO:");
  assert(c.verb == PCMD_GOTO && !c.ok);
  c = P("GOTO:12 34");
  assert(c.verb == PCMD_GOTO && !c.ok);

  c = P("ANGLE:90");
  assert(c.verb == PCMD_ANGLE && c.ok && c.num == 90);
  c = P("ANGLE:9x");
  assert(c.verb == PCMD_ANGLE && !c.ok);

  /* --- ACTUATE ---------------------------------------------------------- */
  c = P("ACTUATE:1");
  assert(c.verb == PCMD_ACTUATE && c.ok && c.num == 1);
  c = P("ACTUATE:0");
  assert(c.verb == PCMD_ACTUATE && c.ok && c.num == 0);
  c = P("ACTUATE:2");            /* only 0 and 1 are in the spec */
  assert(c.verb == PCMD_ACTUATE && !c.ok);
  c = P("ACTUATE:on");
  assert(c.verb == PCMD_ACTUATE && !c.ok);

  /* --- BATT / STATUS are bidirectional; the param decides which ------- */
  c = P("BATT:?");
  assert(c.verb == PCMD_BATT_Q);
  c = P("BATT");
  assert(c.verb == PCMD_BATT_Q);
  c = P("BATT:87");
  assert(c.verb == PCMD_BATT_VAL && c.ok && c.num == 87);
  c = P("BATT:101");             /* out of range */
  assert(c.verb == PCMD_BATT_VAL && !c.ok);

  c = P("STATUS:?");
  assert(c.verb == PCMD_STATUS_Q);
  c = P("STATUS:IDLE");
  assert(c.verb == PCMD_STATUS_VAL && c.ok && c.num == STATE_IDLE);
  c = P("STATUS:MOVING");
  assert(c.verb == PCMD_STATUS_VAL && c.ok && c.num == STATE_MOVING);
  c = P("STATUS:ERROR");
  assert(c.verb == PCMD_STATUS_VAL && c.ok && c.num == STATE_ERROR);
  c = P("STATUS:SLEEPING");
  assert(c.verb == PCMD_STATUS_VAL && !c.ok);

  /* --- a peer that used println() sends "\r\n" ------------------------- */
  c = P("STATUS:IDLE\r\n");
  assert(c.verb == PCMD_STATUS_VAL && c.ok && !strcmp(c.arg, "IDLE"));
  c = P("  GOTO:1200  \r\n");
  assert(c.verb == PCMD_GOTO && c.ok && c.num == 1200);

  /* --- low -> high verbs ---------------------------------------------- */
  c = P("ACK:GOTO");
  assert(c.verb == PCMD_ACK && !strcmp(c.arg, "GOTO"));
  c = P("DONE:ACTUATE");
  assert(c.verb == PCMD_DONE && !strcmp(c.arg, "ACTUATE"));
  c = P("ERR:LOW_BATTERY");
  assert(c.verb == PCMD_ERR && !strcmp(c.arg, "LOW_BATTERY"));

  /* --- degenerate input must not be mistaken for a command ------------ */
  assert(P("").verb        == PCMD_NONE);
  assert(P("\r\n").verb    == PCMD_NONE);
  assert(P("   ").verb     == PCMD_NONE);
  assert(P(":1200").verb   == PCMD_NONE);
  assert(P(NULL).verb      == PCMD_NONE);
  assert(P("GOT:1").verb   == PCMD_UNKNOWN);   /* prefix must not match */
  assert(P("GOTOX:1").verb == PCMD_UNKNOWN);
  assert(P("goto:1").verb  == PCMD_UNKNOWN);   /* spec is upper case */

  /* A param longer than the buffer is truncated, never overflowed. */
  {
    char longLine[PROTO_MAX_LINE * 3];
    memset(longLine, 'x', sizeof longLine - 1);
    longLine[sizeof longLine - 1] = '\0';
    memcpy(longLine, "ERR:", 4);
    c = P(longLine);
    assert(c.verb == PCMD_ERR);
    assert(strlen(c.arg) == PROTO_MAX_LINE - 1);
  }

  /* --- formatting: exactly one '\n', never a truncated line ----------- */
  n = proto_fmt(buf, sizeof buf, "GOTO", "1200");
  assert(n == 10 && !memcmp(buf, "GOTO:1200\n", 10) && buf[n] == '\0');
  assert(strchr(buf, '\r') == NULL);        /* println() would put one here */
  assert(strchr(buf, '\n') == strrchr(buf, '\n'));

  n = proto_fmt(buf, sizeof buf, "STATUS", "");
  assert(n == 8 && !strcmp(buf, "STATUS:\n"));

  /* Truncating would hand the peer a different, valid-looking command. */
  assert(proto_fmt(small, sizeof small, "STATUS", "MOVING") == 0);
  assert(proto_fmt(buf, sizeof buf, NULL, "x") == 0);

  /* Round trip: everything we format, we can parse back. */
  n = proto_fmt(buf, sizeof buf, "BATT", "87");
  assert(n);
  c = P(buf);
  assert(c.verb == PCMD_BATT_VAL && c.ok && c.num == 87);

  /* --- state names match the spec's wording exactly ------------------- */
  assert(!strcmp(proto_state_name(STATE_IDLE), "IDLE"));
  assert(!strcmp(proto_state_name(STATE_MOVING), "MOVING"));
  assert(!strcmp(proto_state_name(STATE_ERROR), "ERROR"));

  puts("protocol.h: all checks passed");
  return 0;
}
