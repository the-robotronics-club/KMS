/* Host self-check for portal_scrape.h.
 *
 * The FortiGate magic token is per-session, so a POST built with a truncated or
 * missing magic fails identically to a wrong password. Every failure mode below
 * has to return 0 rather than a half-value.
 *
 *   cc -Wall -Wextra -o /tmp/test_portal_scrape \
 *      firmware/tools/test_portal_scrape.c && /tmp/test_portal_scrape
 *
 * ponytail: one runnable check, no framework.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../cabinet_esp/portal_scrape.h"

#define TAG "name=\"magic\" value=\""

/* Trimmed from the real https://login.iitmandi.ac.in:1003/login? response —
 * same attribute order and spacing the portal actually sends. */
static const char REAL_FORM[] =
  "<form class=\"login-form\" action=\"/\" method=\"post\">"
  "<input type=\"hidden\" name=\"4Tredir\" "
    "value=\"https://login.iitmandi.ac.in:1003/portal?\">"
  "<input type=\"hidden\" name=\"magic\" value=\"046b8ea5ce7bcf0c\">"
  "<input type=\"hidden\" name=\"\" value=\"\">"
  "<input class=\"form-input\" placeholder=\"Username\" name=\"username\""
    " id=\"ft_un\" type=\"text\">"
  "<input class=\"form-input\" placeholder=\"Password\" name=\"password\""
    " id=\"ft_pd\" type=\"password\">"
  "</form>";

int main(void) {
  char out[33];                        /* matches PORTAL_MAGIC_MAX in config.h */

  /* --- the real page ---------------------------------------------------- */
  assert(scrape_attr(REAL_FORM, TAG, out, sizeof out) == 1);
  assert(!strcmp(out, "046b8ea5ce7bcf0c"));

  /* The 4Tredir input sits BEFORE magic and also ends in `value="`. Proves the
   * match is anchored on the whole tag, not just on `value="`. Needs its own
   * buffer: the redirect URL is 41 chars and will not fit in a magic-sized one,
   * which the refusal below also demonstrates. */
  {
    char redir[64];
    assert(scrape_attr(REAL_FORM, "name=\"4Tredir\" value=\"",
                       redir, sizeof redir) == 1);
    assert(!strcmp(redir, "https://login.iitmandi.ac.in:1003/portal?"));
    assert(scrape_attr(REAL_FORM, "name=\"4Tredir\" value=\"",
                       out, sizeof out) == 0);   /* 41 chars into 33 bytes */
  }

  /* --- absent -----------------------------------------------------------
   * This is the post-login "Authentication Successful" page: same form, no
   * magic. Must fail, not return stale stack. */
  {
    const char *success_page =
      "<form class=\"login-form\" action=\"/\" method=\"post\">"
      "<h2>Authentication Successful</h2></form>";
    memset(out, 'x', sizeof out);
    assert(scrape_attr(success_page, TAG, out, sizeof out) == 0);
    assert(out[0] == '\0');            /* cleared, not left as 'xxx...' */
  }

  /* --- empty input ------------------------------------------------------ */
  assert(scrape_attr("", TAG, out, sizeof out) == 0);
  assert(scrape_attr(REAL_FORM, "", out, sizeof out) == 0);

  /* --- unterminated value: no closing quote before end of buffer -------- */
  assert(scrape_attr("<input name=\"magic\" value=\"046b8ea5", TAG,
                     out, sizeof out) == 0);
  assert(out[0] == '\0');

  /* --- present but empty ------------------------------------------------ */
  assert(scrape_attr("<input name=\"magic\" value=\"\">", TAG,
                     out, sizeof out) == 0);

  /* --- oversized: refuse, never truncate -------------------------------
   * A trimmed magic would POST cleanly and be rejected as a bad password, so
   * the wrong behaviour here costs an hour of debugging the credential. */
  {
    char small[8];                     /* 16 hex chars will not fit */
    assert(scrape_attr(REAL_FORM, TAG, small, sizeof small) == 0);
    assert(small[0] == '\0');
  }

  /* Exact fit: 16 chars + NUL in 17 bytes. */
  {
    char exact[17];
    assert(scrape_attr(REAL_FORM, TAG, exact, sizeof exact) == 1);
    assert(!strcmp(exact, "046b8ea5ce7bcf0c"));
  }
  /* One byte short of exact must refuse. */
  {
    char tight[16];
    assert(scrape_attr(REAL_FORM, TAG, tight, sizeof tight) == 0);
  }

  /* --- cap == 0 must not write --------------------------------------- */
  assert(scrape_attr(REAL_FORM, TAG, out, 0) == 0);

  /* --- partial tag at end of buffer must not overread ------------------- */
  assert(scrape_attr("name=\"magic\" value=", TAG, out, sizeof out) == 0);

  printf("portal_scrape.h: all checks passed\n");
  return 0;
}
