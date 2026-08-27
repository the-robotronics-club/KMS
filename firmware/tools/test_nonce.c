/* Host self-check for nonce.h. No hardware, no Arduino headers.
     cc -Wall -Wextra -o /tmp/tn firmware/tools/test_nonce.c && /tmp/tn        */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../cabinet_esp/nonce.h"

int main(void) {
  /* --- the case that was silently dropping every dispense ---------------- */
  /* secrets.token_hex(16): 32 hex chars, leading letter. The old
     doc["nonce"] | 0UL read this as 0 and the command was discarded. */
  assert(nonce_fold("a3f9c2d1e4b5a6978877665544332211") == 0x44332211UL);
  assert(nonce_fold("A3F9C2D1E4B5A6978877665544332211") == 0x44332211UL);

  /* --- bring-up nonces sent by hand ------------------------------------- */
  assert(nonce_fold("12345") == 0x12345UL);
  assert(nonce_fold("1") == 1UL);

  /* --- short and exact-length inputs use the whole string ---------------- */
  assert(nonce_fold("ff") == 0xffUL);
  assert(nonce_fold("deadbeef") == 0xdeadbeefUL);
  /* 9 chars: only the last 8 count. */
  assert(nonce_fold("1deadbeef") == 0xdeadbeefUL);

  /* --- 0 is reserved for "absent", so nothing real may fold to it -------- */
  assert(nonce_fold(NULL) == 0);
  assert(nonce_fold("") == 0);
  assert(nonce_fold("00000000") == 1UL);
  assert(nonce_fold("0") == 1UL);
  assert(nonce_fold("ffffffff00000000") == 1UL);

  /* --- anything that is not a nonce is rejected, not half-parsed --------- */
  assert(nonce_fold("not-a-nonce") == 0);
  assert(nonce_fold("dead beef") == 0);
  assert(nonce_fold("dead\nbeef") == 0);
  /* Junk in the *tail* is what a truncated payload looks like. */
  assert(nonce_fold("a3f9c2d1e4b5a69788776655443322!!") == 0);

  /* --- distinct nonces stay distinct, or the ring would false-positive --- */
  assert(nonce_fold("a3f9c2d1e4b5a6978877665544332211") !=
         nonce_fold("a3f9c2d1e4b5a6978877665544332212"));

  /* --- the verbatim copy the rack event echoes back must fit ------------- */
  assert(strlen("a3f9c2d1e4b5a6978877665544332211") < NONCE_STR_MAX);

  puts("nonce.h: all checks passed");
  return 0;
}
