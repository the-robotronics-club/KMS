#ifndef NONCE_H
#define NONCE_H

/*
  Replay-nonce fingerprinting.

  The backend mints nonces with secrets.token_hex(16) — 32 hex characters, sent
  as a JSON *string*:

      {"action":"dispense","slot_number":3,"nonce":"a3f9c2d1e4b5a697..."}

  The replay ring only has to answer "have I run this command already", so it
  stores a 32-bit fingerprint rather than 64 strings. 64 x uint32_t is 256 bytes;
  64 Arduino Strings would be ~3 KB of heap fragmentation next to a TLS
  handshake that needs a large contiguous block.

  The fingerprint is the low 32 bits of the hex nonce. Those bits come straight
  from a CSPRNG, so over a 64-entry window the collision chance is ~5e-7 — and a
  collision only drops one command, which the caller retries. Uniqueness proper
  is still enforced backend-side in Redis (consume_nonce), not here.

  A bare JSON number is also accepted, because that is what a hand-sent
  mosquitto_pub during bring-up looks like:

      -m '{"action":"dispense","slot_number":3,"nonce":12345}'

  Pure C, no Arduino headers, so firmware/tools/test_nonce.c can exercise it on
  the host.
*/

#include <stddef.h>
#include <stdint.h>

/* Longest nonce we keep verbatim: 32 hex chars + NUL, rounded up for slack. */
#define NONCE_STR_MAX 40

/* 0 is the caller's "absent / unparseable" sentinel, so a real nonce never
   folds to it. */
static inline uint32_t nonce_fold(const char *s) {
  if (!s) return 0;

  size_t n = 0;
  while (s[n]) n++;
  if (!n) return 0;

  /* Hex string: take the low 32 bits, i.e. the last 8 hex digits. A decimal
     nonce parses here too — its digits are a subset of hex — which is fine,
     the value only has to be stable and well distributed, not faithful. */
  const char *tail = (n > 8) ? s + n - 8 : s;
  uint32_t v = 0;
  int digits = 0;
  for (; *tail; tail++) {
    int d;
    if (*tail >= '0' && *tail <= '9')      d = *tail - '0';
    else if (*tail >= 'a' && *tail <= 'f') d = *tail - 'a' + 10;
    else if (*tail >= 'A' && *tail <= 'F') d = *tail - 'A' + 10;
    else return 0;                          /* not a nonce we understand */
    v = (v << 4) | (uint32_t)d;
    digits++;
  }
  if (!digits) return 0;
  return v ? v : 1UL;                       /* keep 0 reserved */
}

#endif /* NONCE_H */
