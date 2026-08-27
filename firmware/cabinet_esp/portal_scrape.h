#ifndef PORTAL_SCRAPE_H
#define PORTAL_SCRAPE_H

/* Pulls the FortiGate `magic` token out of the captive-portal login form.
 *
 * Pure C, like protocol.h and urlencode.h, so tools/test_portal_scrape.c can
 * prove it on the host against real captured HTML.
 *
 * The token is per-session — every GET of the form returns a different one — so
 * a POST built with a stale or truncated magic is rejected exactly as a wrong
 * password would be. That makes a silent bug here indistinguishable from bad
 * credentials, which is why it is worth its own test.
 */

#include <stddef.h>
#include <string.h>

/* Finds `tag` in `html` and copies the run of characters after it up to the
 * next '"' into `out`.
 *
 * Returns 1 on success, 0 if the tag is absent, the value is unterminated, the
 * value is empty, or it does not fit in cap. `out` is always NUL-terminated on
 * success; on failure it is left as an empty string so a caller that ignores
 * the return value cannot POST uninitialised stack.
 *
 * Deliberately not an HTML parser: it matches one exact literal. If the portal
 * reorders its attributes the match fails loudly rather than picking up the
 * wrong value, which is the behaviour we want on a credential path.
 */
static inline int scrape_attr(const char *html, const char *tag,
                              char *out, size_t cap) {
  size_t i, j;

  if (cap == 0) return 0;
  out[0] = '\0';
  if (!html || !tag || !*tag) return 0;

  /* Plain substring search. The form is a few KB and this runs at most once per
   * login attempt, so there is nothing to gain from anything cleverer. */
  const char *p = html;
  const size_t taglen = strlen(tag);
  for (;; p++) {
    for (i = 0; i < taglen && p[i] == tag[i]; i++) { }
    if (i == taglen) break;              /* matched */
    if (!*p) return 0;                   /* ran out before matching */
  }
  p += taglen;

  for (j = 0; p[j] && p[j] != '"'; j++) { }
  if (p[j] != '"') return 0;             /* unterminated value */
  if (j == 0) return 0;                  /* value present but empty */
  if (j + 1 > cap) return 0;             /* would not fit; refuse, never trim */

  for (i = 0; i < j; i++) out[i] = p[i];
  out[j] = '\0';
  return 1;
}

#endif /* PORTAL_SCRAPE_H */
