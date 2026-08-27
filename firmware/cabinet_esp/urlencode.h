#ifndef URLENCODE_H
#define URLENCODE_H

/* Percent-encoding for the captive-portal login form body.
 *
 * Pure C, like protocol.h, so tools/test_urlencode.c can prove it on the host.
 * A campus password containing '&', '+' or '%' silently truncates or corrupts an
 * x-www-form-urlencoded body, and the portal answers exactly as it would for a
 * wrong password — so a bug here is indistinguishable from bad credentials.
 */

#include <stddef.h>

/* Encodes everything outside RFC 3986's unreserved set. Truncates rather than
 * overflows if cap is too small, and always NUL-terminates. cap must be >= 1.
 */
static inline void url_encode(const char *in, char *out, size_t cap) {
  static const char hex[] = "0123456789ABCDEF";
  size_t o = 0;
  if (cap == 0) return;
  for (const char *p = in; *p; p++) {
    const unsigned char c = (unsigned char)*p;
    const int unreserved = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                           c == '.' || c == '~';
    /* Reserve room for the whole escape plus the NUL, never a partial "%4". */
    const size_t need = unreserved ? 1u : 3u;
    if (o + need + 1 > cap) break;
    if (unreserved) {
      out[o++] = (char)c;
    } else {
      out[o++] = '%';
      out[o++] = hex[c >> 4];
      out[o++] = hex[c & 0x0F];
    }
  }
  out[o] = '\0';
}

#endif /* URLENCODE_H */
