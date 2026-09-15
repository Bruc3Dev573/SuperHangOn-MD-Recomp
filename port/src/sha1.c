/* SHA-1 (FIPS 180-4), used to check the ROM the game code was built for. */
#include <string.h>
#include "sha1.h"

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void block(uint32_t h[5], const uint8_t *p)
{
  uint32_t w[80];
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t)p[i * 4] << 24 | p[i * 4 + 1] << 16 | p[i * 4 + 2] << 8 | p[i * 4 + 3];
  for (int i = 16; i < 80; i++)
    w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
    else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
    else { f = b ^ c ^ d; k = 0xca62c1d6; }
    uint32_t t = rol(a, 5) + f + e + k + w[i];
    e = d; d = c; c = rol(b, 30); b = a; a = t;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void sha1(const uint8_t *data, size_t len, char hex[41])
{
  uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
  size_t i = 0;
  for (; i + 64 <= len; i += 64)
    block(h, data + i);
  uint8_t tail[128] = {0};
  size_t rest = len - i;
  memcpy(tail, data + i, rest);
  tail[rest] = 0x80;
  size_t n = rest + 1 + 8 <= 64 ? 64 : 128;
  uint64_t bits = (uint64_t)len * 8;
  for (int k = 0; k < 8; k++)
    tail[n - 1 - k] = (uint8_t)(bits >> (k * 8));
  block(h, tail);
  if (n == 128)
    block(h, tail + 64);
  static const char digits[] = "0123456789abcdef";
  for (int k = 0; k < 20; k++) {
    uint8_t byte = (uint8_t)(h[k / 4] >> (24 - (k % 4) * 8));
    hex[k * 2] = digits[byte >> 4];
    hex[k * 2 + 1] = digits[byte & 15];
  }
  hex[40] = 0;
}
