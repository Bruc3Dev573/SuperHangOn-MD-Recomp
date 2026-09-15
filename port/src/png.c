/* Minimal PNG writer (uncompressed deflate blocks) for screenshots. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "png.h"

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
  if (!crc_table[1])
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t v = i;
      for (int k = 0; k < 8; k++) v = v & 1 ? 0xedb88320u ^ (v >> 1) : v >> 1;
      crc_table[i] = v;
    }
  c ^= 0xffffffffu;
  while (n--) c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
  return c ^ 0xffffffffu;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
  uint8_t h[8];
  be32(h, len);
  memcpy(h + 4, type, 4);
  fwrite(h, 1, 8, f);
  if (len) fwrite(data, 1, len, f);
  uint32_t c = crc(0, (const uint8_t *)type, 4);
  c = crc(c, data, len);
  be32(h, c);
  fwrite(h, 1, 4, f);
}

int png_write_rgb32(const char *path, const uint32_t *pixels, int w, int h, int stride)
{
  size_t raw_len = (size_t)(w * 3 + 1) * h;
  uint8_t *raw = malloc(raw_len);
  for (int y = 0; y < h; y++) {
    uint8_t *row = raw + y * (w * 3 + 1);
    row[0] = 0;
    for (int x = 0; x < w; x++) {
      uint32_t p = pixels[y * stride + x];
      row[1 + x * 3] = p >> 16; row[2 + x * 3] = p >> 8; row[3 + x * 3] = p;
    }
  }
  size_t blocks = raw_len / 65535 + 1;
  uint8_t *z = malloc(raw_len + blocks * 5 + 6), *q = z;
  *q++ = 0x78; *q++ = 0x01;
  uint32_t s1 = 1, s2 = 0;
  for (size_t i = 0; i < raw_len; i++) { s1 = (s1 + raw[i]) % 65521; s2 = (s2 + s1) % 65521; }
  for (size_t pos = 0, left = raw_len; left;) {
    unsigned n = left > 65535 ? 65535 : (unsigned)left;
    left -= n;
    *q++ = left ? 0 : 1;
    *q++ = n & 0xff; *q++ = n >> 8; *q++ = ~n & 0xff; *q++ = (~n >> 8) & 0xff;
    memcpy(q, raw + pos, n);
    q += n; pos += n;
  }
  be32(q, (s2 << 16) | s1); q += 4;
  FILE *f = fopen(path, "wb");
  if (!f) { free(raw); free(z); return -1; }
  fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
  uint8_t ihdr[13];
  be32(ihdr, w); be32(ihdr + 4, h);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
  chunk(f, "IHDR", ihdr, 13);
  chunk(f, "IDAT", z, (uint32_t)(q - z));
  chunk(f, "IEND", NULL, 0);
  fclose(f);
  free(raw); free(z);
  return 0;
}
