/* Host self-check for urlencode.h.
 *
 * This runs on the credential path: a password with '&' in it must not be able
 * to split the login form body, and a truncating buffer must not emit a partial
 * escape like "%4" that the portal would decode as garbage.
 *
 *   cc -Wall -Wextra -o /tmp/test_urlencode firmware/tools/test_urlencode.c \
 *      && /tmp/test_urlencode
 *
 * ponytail: one runnable check, no framework.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../cabinet_esp/urlencode.h"

int main(void) {
  char out[64];

  /* --- unreserved set passes through untouched -------------------------- */
  url_encode("abcXYZ019-_.~", out, sizeof out);
  assert(!strcmp(out, "abcXYZ019-_.~"));

  url_encode("", out, sizeof out);
  assert(!strcmp(out, ""));

  /* --- the characters that break a form body --------------------------- */
  url_encode("&", out, sizeof out);
  assert(!strcmp(out, "%26"));
  url_encode("=", out, sizeof out);
  assert(!strcmp(out, "%3D"));
  url_encode("+", out, sizeof out);
  assert(!strcmp(out, "%2B"));
  url_encode("%", out, sizeof out);
  assert(!strcmp(out, "%25"));
  url_encode(" ", out, sizeof out);
  assert(!strcmp(out, "%20"));

  /* A password that would otherwise inject a field of its own. */
  url_encode("p&admin=1", out, sizeof out);
  assert(!strcmp(out, "p%26admin%3D1"));

  /* --- high bytes encode as unsigned, not sign-extended ----------------- */
  url_encode("\xC3\xA9", out, sizeof out);      /* U+00E9 in UTF-8 */
  assert(!strcmp(out, "%C3%A9"));

  /* --- truncation is clean: never a partial escape ---------------------- */
  {
    char small[5];                 /* room for one escape + NUL, not two */
    url_encode("&&", small, sizeof small);
    assert(!strcmp(small, "%26"));
  }
  {
    char small[3];                 /* not even one escape fits */
    url_encode("&", small, sizeof small);
    assert(!strcmp(small, ""));
  }
  {
    char small[3];
    url_encode("ab", small, sizeof small);
    assert(!strcmp(small, "ab"));
  }
  {
    char small[3];
    url_encode("abc", small, sizeof small);
    assert(!strcmp(small, "ab"));  /* truncated, still terminated */
  }
  {
    char one[1];
    url_encode("abc", one, sizeof one);
    assert(one[0] == '\0');
  }

  printf("urlencode.h: all checks passed\n");
  return 0;
}
