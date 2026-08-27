#ifndef SECURE_LINK_H
#define SECURE_LINK_H

/*
  Encrypted transport for the ASCII protocol in protocol.h.

  Wire frame (unchanged from the original sketches, so a half-migrated bench
  setup still syncs):

      0xAA 0x55 len_hi len_lo [nonce 16][ciphertext N][mac 32]
      len = 16 + N + 32

  The plaintext inside is one protocol.h line, e.g. "GOTO:1200\n". Protocol
  compliance and confidentiality are orthogonal: the spec governs the payload,
  this file governs how the payload crosses the wire. Sending the spec's ASCII
  bare over UART would satisfy protocol.h and hand anyone with a probe on the
  two header pins the ability to open the cabinet, so the frame stays.

  Requires the ESP32 Arduino core (mbedTLS). The ESP8266 core ships no
  mbedtls/aes.h, which is why both ends of this link are ESP32.

  IDENTICAL COPY in firmware/cabinet_esp/ and firmware/electronics_esp/.
  Both ends must derive keys the same way; edit one, edit the other.
*/

#include <Arduino.h>
#include "mbedtls/aes.h"
#include "mbedtls/md.h"

#include "protocol.h"

#define SL_SYNC1        0xAA
#define SL_SYNC2        0x55
#define SL_NONCE_LEN    16
#define SL_MAC_LEN      32
#define SL_MAX_PLAIN    PROTO_MAX_LINE
#define SL_MIN_PAYLOAD  (SL_NONCE_LEN + SL_MAC_LEN)
#define SL_MAX_PAYLOAD  (SL_NONCE_LEN + SL_MAX_PLAIN + SL_MAC_LEN)

/* Replay window. Commands are one line each, so 16 remembered nonces covers
   far more than the link ever has in flight.
   ponytail: linear scan over 16 entries; make it a hash set if the frame rate
   ever climbs past a few hundred per second. */
#define SL_NONCE_RING   16

/* Distinct labels so the AES key and the HMAC key are never the same bytes.
   The original sketches passed one 32-byte array to both
   mbedtls_aes_setkey_enc() and mbedtls_md_hmac_starts() — one key doing two
   jobs, which is the flaw the labels remove. */
#define SL_LABEL_ENC "kms-uart-enc-v1"
#define SL_LABEL_MAC "kms-uart-mac-v1"

/* Compares in constant time. A plain memcmp() returns on the first differing
   byte, so its runtime leaks how many leading bytes matched — enough to forge
   a MAC one byte at a time over many tries. */
static inline bool sl_ct_equal(const uint8_t *a, const uint8_t *b, size_t n) {
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

static inline bool sl_hmac(const uint8_t *key, size_t keyLen,
                           const uint8_t *d1, size_t n1,
                           const uint8_t *d2, size_t n2,
                           uint8_t out[32]) {
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_context_t ctx;
  bool ok = false;
  mbedtls_md_init(&ctx);
  if (mbedtls_md_setup(&ctx, info, 1) == 0
      && mbedtls_md_hmac_starts(&ctx, key, keyLen) == 0
      && (!n1 || mbedtls_md_hmac_update(&ctx, d1, n1) == 0)
      && (!n2 || mbedtls_md_hmac_update(&ctx, d2, n2) == 0)
      && mbedtls_md_hmac_finish(&ctx, out) == 0) {
    ok = true;
  }
  mbedtls_md_free(&ctx);
  return ok;
}

/* K_enc = HMAC(master, "kms-uart-enc-v1"), K_mac = HMAC(master, "kms-uart-mac-v1").
   One HMAC each is sufficient here: both outputs are exactly 32 bytes, so the
   iteration counter a full HKDF-Expand adds would never advance. */
static inline bool sl_derive(const uint8_t master[32],
                             uint8_t k_enc[32], uint8_t k_mac[32]) {
  return sl_hmac(master, 32, (const uint8_t *)SL_LABEL_ENC,
                 strlen(SL_LABEL_ENC), NULL, 0, k_enc)
      && sl_hmac(master, 32, (const uint8_t *)SL_LABEL_MAC,
                 strlen(SL_LABEL_MAC), NULL, 0, k_mac);
}

class SecureLink {
 public:
  void begin(Stream *port, const uint8_t master[32]) {
    port_ = port;
    keysOk_ = sl_derive(master, kEnc_, kMac_);
    state_ = WAIT_SYNC1;
    memset(nonceRing_, 0, sizeof(nonceRing_));
    ringHead_ = 0;
  }

  /* Encrypts and writes one protocol line. len excludes the NUL. */
  bool send(const char *line, size_t len) {
    uint8_t nonce[SL_NONCE_LEN];
    uint8_t ct[SL_MAX_PLAIN];
    uint8_t mac[SL_MAC_LEN];
    uint8_t streamBlock[16];
    uint8_t counter[SL_NONCE_LEN];
    mbedtls_aes_context aes;
    unsigned int ncOff = 0;
    uint16_t total;
    int rc;

    if (!port_ || !keysOk_ || !len || len > SL_MAX_PLAIN) return false;

    for (int i = 0; i < SL_NONCE_LEN / 4; i++) {
      uint32_t r = esp_random();
      memcpy(nonce + i * 4, &r, 4);
    }

    mbedtls_aes_init(&aes);
    /* AES-256: the whole derived key, not the first half of it. */
    rc = mbedtls_aes_setkey_enc(&aes, kEnc_, 256);
    if (rc == 0) {
      memcpy(counter, nonce, SL_NONCE_LEN);
      rc = mbedtls_aes_crypt_ctr(&aes, len, &ncOff, counter, streamBlock,
                                 (const uint8_t *)line, ct);
    }
    mbedtls_aes_free(&aes);
    if (rc != 0) return false;

    /* MAC covers nonce||ciphertext, so a replayed or retargeted nonce fails
       verification rather than just decrypting to garbage. */
    if (!sl_hmac(kMac_, 32, nonce, SL_NONCE_LEN, ct, len, mac)) return false;

    total = (uint16_t)(SL_NONCE_LEN + len + SL_MAC_LEN);
    port_->write((uint8_t)SL_SYNC1);
    port_->write((uint8_t)SL_SYNC2);
    port_->write((uint8_t)((total >> 8) & 0xFF));
    port_->write((uint8_t)(total & 0xFF));
    port_->write(nonce, SL_NONCE_LEN);
    port_->write(ct, len);
    port_->write(mac, SL_MAC_LEN);
    return true;
  }

  bool send(const char *line) { return send(line, strlen(line)); }

  /* Drains the port without blocking and returns the decrypted line length
     once a whole authenticated frame has landed, else 0. out is
     NUL-terminated. Never calls readStringUntil() — that blocks for up to the
     stream timeout (1 s by default), which on the low level means
     AccelStepper::run() is not called for a second and the motor stalls
     mid-travel. */
  size_t poll(char *out, size_t cap) {
    if (!port_) return 0;
    while (port_->available()) {
      uint8_t b = (uint8_t)port_->read();
      switch (state_) {
        case WAIT_SYNC1:
          if (b == SL_SYNC1) state_ = WAIT_SYNC2;
          break;
        case WAIT_SYNC2:
          /* 0xAA 0xAA must not drop the second 0xAA: it may be the real sync1
             of the next frame. */
          if (b == SL_SYNC2)      state_ = READ_LEN_HI;
          else if (b == SL_SYNC1) state_ = WAIT_SYNC2;
          else                    state_ = WAIT_SYNC1;
          break;
        case READ_LEN_HI:
          payloadLen_ = (uint16_t)b << 8;
          state_ = READ_LEN_LO;
          break;
        case READ_LEN_LO:
          payloadLen_ |= b;
          /* Bounds-check BEFORE reserving anything. The original allocated
             malloc(payloadLen) for anything up to 4096 on an unauthenticated
             length field and never checked the result, so two spoofed header
             bytes were a remote heap exhaustion plus a null-pointer write. */
          if (payloadLen_ < SL_MIN_PAYLOAD || payloadLen_ > SL_MAX_PAYLOAD) {
            state_ = WAIT_SYNC1;
          } else {
            bufPos_ = 0;
            state_ = READ_PAYLOAD;
          }
          break;
        case READ_PAYLOAD:
          buf_[bufPos_++] = b;
          if (bufPos_ >= payloadLen_) {
            size_t n = openFrame_(out, cap);
            state_ = WAIT_SYNC1;
            if (n) return n;   /* one line per poll; rest stays buffered */
          }
          break;
      }
    }
    return 0;
  }

  uint32_t rejected() const { return rejected_; }

 private:
  size_t openFrame_(char *out, size_t cap) {
    const uint8_t *nonce = buf_;
    size_t clen = payloadLen_ - SL_NONCE_LEN - SL_MAC_LEN;
    const uint8_t *ct  = buf_ + SL_NONCE_LEN;
    const uint8_t *mac = buf_ + SL_NONCE_LEN + clen;
    uint8_t calc[SL_MAC_LEN];
    uint8_t streamBlock[16];
    uint8_t counter[SL_NONCE_LEN];
    mbedtls_aes_context aes;
    unsigned int ncOff = 0;
    int rc;

    if (!keysOk_ || clen == 0 || clen + 1 > cap) { rejected_++; return 0; }

    if (!sl_hmac(kMac_, 32, nonce, SL_NONCE_LEN, ct, clen, calc)) {
      rejected_++;
      return 0;
    }
    /* Authenticate before decrypting, and before touching the nonce ring — a
       forged frame must not be able to evict a real nonce from the window. */
    if (!sl_ct_equal(calc, mac, SL_MAC_LEN)) { rejected_++; return 0; }

    if (nonceSeen_(nonce)) { rejected_++; return 0; }
    rememberNonce_(nonce);

    mbedtls_aes_init(&aes);
    rc = mbedtls_aes_setkey_enc(&aes, kEnc_, 256);
    if (rc == 0) {
      memcpy(counter, nonce, SL_NONCE_LEN);
      rc = mbedtls_aes_crypt_ctr(&aes, clen, &ncOff, counter, streamBlock,
                                 ct, (uint8_t *)out);
    }
    mbedtls_aes_free(&aes);
    if (rc != 0) { rejected_++; return 0; }

    out[clen] = '\0';
    return clen;
  }

  /* First 8 nonce bytes identify a frame: the nonce is 128 bits of hardware
     RNG, so 64 bits of it colliding by chance is not a scenario this link
     reaches. */
  bool nonceSeen_(const uint8_t *nonce) const {
    for (int i = 0; i < SL_NONCE_RING; i++) {
      if (!memcmp(nonceRing_[i], nonce, 8)) return true;
    }
    return false;
  }

  void rememberNonce_(const uint8_t *nonce) {
    memcpy(nonceRing_[ringHead_], nonce, 8);
    ringHead_ = (uint8_t)((ringHead_ + 1) % SL_NONCE_RING);
  }

  enum { WAIT_SYNC1, WAIT_SYNC2, READ_LEN_HI, READ_LEN_LO, READ_PAYLOAD };

  Stream  *port_ = nullptr;
  uint8_t  kEnc_[32] = {0};
  uint8_t  kMac_[32] = {0};
  bool     keysOk_ = false;

  uint8_t  state_ = WAIT_SYNC1;
  uint16_t payloadLen_ = 0;
  size_t   bufPos_ = 0;
  uint8_t  buf_[SL_MAX_PAYLOAD];      /* fixed — no malloc on the RX path */

  uint8_t  nonceRing_[SL_NONCE_RING][8];
  uint8_t  ringHead_ = 0;
  uint32_t rejected_ = 0;
};

#endif /* SECURE_LINK_H */
