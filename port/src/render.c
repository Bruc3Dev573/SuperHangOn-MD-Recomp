/*
 * VDP mode 5 renderer: planes A/B, window, sprites, per-line scrolling and
 * priorities, into a 32-bit RGB frame (320x224 or 256x224).
 */
#include <string.h>
#include "md.h"
#include "render.h"
#include "state.h"

uint32_t render_frame_rgb[MD_MAX_H][MD_MAX_W];
int render_width = 320;

int render_gpgx_colours;
static int spr_ovr;           /* sprite masking state carried to the next line */

static uint32_t cram_rgb(uint16_t c)
{
  uint32_t r = (c >> 1) & 7, g = (c >> 5) & 7, b = (c >> 9) & 7;
  if (render_gpgx_colours) {
    /* same quantisation as the reference: 4-bit level (x << 1) packed to RGB565
     * by Genesis Plus GX, expanded to 8 bits by the tracer's PNG writer */
    uint32_t r5 = (r << 2) | (r >> 2), g6 = (g << 3) | (g >> 1), b5 = (b << 2) | (b >> 2);
    return ((r5 * 255 / 31) << 16) | ((g6 * 255 / 63) << 8) | (b5 * 255 / 31);
  }
  r = r * 255 / 7; g = g * 255 / 7; b = b * 255 / 7;
  return (r << 16) | (g << 8) | b;
}

uint32_t render_colour(unsigned index)
{
  return cram_rgb(vdp.cram[index & 0x3f]);
}

static inline uint16_t vram_word(uint32_t a)
{
  return (uint16_t)((vdp.vram[a & 0xffff] << 8) | vdp.vram[(a + 1) & 0xffff]);
}

/* pixel of a plane tile: returns (priority << 8) | (palette << 4) | colour */
static inline unsigned tile_pixel(uint16_t entry, unsigned px, unsigned py)
{
  unsigned tile = entry & 0x7ff;
  if (entry & 0x0800) px = 7 - px;
  if (entry & 0x1000) py = 7 - py;
  uint8_t b = vdp.vram[((tile << 5) + (py << 2) + (px >> 1)) & 0xffff];
  unsigned col = (px & 1) ? (b & 0x0f) : (b >> 4);
  return ((entry >> 15) << 8) | (((entry >> 13) & 3) << 4) | col;
}

void render_line(int y, uint16_t vscroll_a, uint16_t vscroll_b, uint32_t *out, int width)
{
  const uint8_t *r = vdp.reg;
  static const unsigned sizes[4] = {32, 64, 64, 128};
  unsigned hcells = sizes[r[16] & 3], vcells = sizes[(r[16] >> 4) & 3];
  unsigned hmask = hcells * 8 - 1, vmask = vcells * 8 - 1;
  uint32_t nt_a = (uint32_t)(r[2] & 0x38) << 10;
  uint32_t nt_b = (uint32_t)(r[4] & 0x07) << 13;
  uint32_t nt_w = (uint32_t)(r[3] & (width == 320 ? 0x3c : 0x3e)) << 10;
  uint32_t hs_table = (uint32_t)r[13] << 10;
  unsigned bg = r[7] & 0x3f;

  if (!(r[1] & 0x40)) {
    uint32_t c = cram_rgb(vdp.cram[bg]);
    for (int x = 0; x < width; x++) out[x] = c;
    return;
  }

  unsigned hs_line;
  switch (r[11] & 3) {
    case 0: hs_line = 0; break;
    case 2: hs_line = (unsigned)y & ~7u; break;
    case 3: hs_line = (unsigned)y; break;
    default: hs_line = (unsigned)y & ~7u; break;
  }
  uint16_t hs_a = vram_word(hs_table + hs_line * 4) & 0x3ff;
  uint16_t hs_b = vram_word(hs_table + hs_line * 4 + 2) & 0x3ff;

  /* window area */
  unsigned wh = r[17] & 0x1f, wv = r[18] & 0x1f;
  int win_line = (r[18] & 0x80) ? ((unsigned)y >= wv * 8) : ((unsigned)y < wv * 8);
  int win_x0 = 0, win_x1 = 0;
  if (win_line) { win_x0 = 0; win_x1 = width; }
  else if (r[17] & 0x80) { win_x0 = wh * 16; win_x1 = width; }
  else { win_x0 = 0; win_x1 = wh * 16; }

  /* sprites for this line, front to back (as Genesis Plus GX: 9-bit Y, 20
   * sprites / 320 pixels per line including off-screen ones, masking by X = 0
   * after a sprite with X != 0 or after a line that reached the pixel limit) */
  uint8_t spr[MD_MAX_W];
  memset(spr, 0, sizeof spr);
  {
    uint32_t sat = (uint32_t)(r[5] & (width == 320 ? 0x7e : 0x7f)) << 9;
    int max_sprites = width == 320 ? 80 : 64, max_line = width == 320 ? 20 : 16;
    int max_pixels = width;
    int n = 0, on_line = 0, idx = 0, masked = 0, pixels = 0, limit = 0;
    do {
      uint32_t e = sat + idx * 8;
      int sy = (vram_word(e) & 0x1ff) - 128;
      uint8_t sz = vdp.vram[(e + 2) & 0xffff];
      uint8_t next = vdp.vram[(e + 3) & 0xffff] & 0x7f;
      int w = (((sz >> 2) & 3) + 1) * 8, h = ((sz & 3) + 1) * 8;
      if (y >= sy && y < sy + h) {
        if (++on_line > max_line)
          break;
        uint16_t attr = vram_word(e + 4);
        int xraw = vram_word(e + 6) & 0x1ff, sx = xraw - 128;
        if (xraw) spr_ovr = 1;
        else if (spr_ovr) masked = 1;
        pixels += w;
        int draw_w = pixels > max_pixels ? w - (pixels - max_pixels) : w;
        if (!masked) {
          int py = y - sy;
          if (attr & 0x1000) py = h - 1 - py;
          for (int px = 0; px < (draw_w & ~7); px++) {
            int x = sx + px;
            if (x < 0 || x >= width) continue;
            if (spr[x] & 0x0f) continue;          /* earlier sprite wins */
            int tx = (attr & 0x0800) ? w - 1 - px : px;
            unsigned tile = (attr & 0x7ff) + (unsigned)((tx >> 3) * (h / 8) + (py >> 3));
            uint8_t b = vdp.vram[((tile << 5) + ((py & 7) << 2) + ((tx & 7) >> 1)) & 0xffff];
            unsigned col = (tx & 1) ? (b & 0x0f) : (b >> 4);
            if (col)
              spr[x] = (uint8_t)((((attr >> 13) & 3) << 4) | col | ((attr >> 15) ? 0x80 : 0));
          }
        }
        if (pixels >= max_pixels) {
          spr_ovr = pixels >= width;
          limit = 1;
          break;
        }
      }
      idx = next;
    } while (idx && idx < max_sprites && ++n < max_sprites);
    if (!limit)
      spr_ovr = 0;
  }

  for (int x = 0; x < width; x++) {
    unsigned pa, pb;
    if (x >= win_x0 && x < win_x1) {
      uint16_t entry = vram_word(nt_w + ((unsigned)(y >> 3) * (width == 320 ? 64 : 32) + (unsigned)(x >> 3)) * 2);
      pa = tile_pixel(entry, x & 7, y & 7);
    } else {
      unsigned ax = ((unsigned)x - hs_a) & hmask;
      unsigned ay = ((unsigned)y + vscroll_a) & vmask;
      uint16_t entry = vram_word(nt_a + ((ay >> 3) * hcells + (ax >> 3)) * 2);
      pa = tile_pixel(entry, ax & 7, ay & 7);
    }
    {
      unsigned bx = ((unsigned)x - hs_b) & hmask;
      unsigned by = ((unsigned)y + vscroll_b) & vmask;
      uint16_t entry = vram_word(nt_b + ((by >> 3) * hcells + (bx >> 3)) * 2);
      pb = tile_pixel(entry, bx & 7, by & 7);
    }
    unsigned s = spr[x];
    unsigned colour = bg;
    int sp_hi = (s & 0x80) != 0, a_hi = (pa >> 8) & 1, b_hi = (pb >> 8) & 1;
    unsigned sc = s & 0x3f, ac = pa & 0x3f, bc = pb & 0x3f;
    /* priority: S hi > A hi > B hi > S lo > A lo > B lo > backdrop */
    if ((sc & 0x0f) && sp_hi) colour = sc;
    else if ((ac & 0x0f) && a_hi) colour = ac;
    else if ((bc & 0x0f) && b_hi) colour = bc;
    else if (sc & 0x0f) colour = sc;
    else if (ac & 0x0f) colour = ac;
    else if (bc & 0x0f) colour = bc;
    out[x] = cram_rgb(vdp.cram[colour]);
  }
}

/* ---- wide layers --------------------------------------------------------- */

uint32_t render_wide_back[MD_MAX_H][RENDER_WIDE_MAX];
uint32_t render_wide_plane_lo[MD_MAX_H][RENDER_WIDE_MAX];
uint32_t render_wide_plane_hi[MD_MAX_H][RENDER_WIDE_MAX];
int render_wide_effects;

/* window tile rows spread over the wide picture: groups of HUD tiles that
 * start in columns 0-15 move to the left edge, 22-39 to the right edge,
 * 16-21 (and groups spanning the middle) stay; source x per wide column, -1
 * where nothing is shown */
static int16_t hud_map[32][RENDER_WIDE_MAX];

static int window_tile_blank(uint32_t nt_w, unsigned tx, unsigned ty)
{
  uint16_t entry = vram_word(nt_w + (ty * 64 + tx) * 2);
  for (unsigned py = 0; py < 8; py++)
    for (unsigned px = 0; px < 8; px++)
      if (tile_pixel(entry, px, py) & 0x0f)
        return 0;
  return 1;
}

static void build_hud_map(uint32_t nt_w, unsigned ty, int ext, int width)
{
  int16_t *map = hud_map[ty];
  for (int i = 0; i < width; i++)
    map[i] = -1;
  int blank[40];
  for (unsigned c = 0; c < 40; c++)
    blank[c] = window_tile_blank(nt_w, c, ty);
  for (int c0 = 0; c0 < 40;) {
    if (blank[c0]) { c0++; continue; }
    int c1 = c0;
    while (c1 + 1 < 40 && !blank[c1 + 1]) c1++;
    int shift = c0 <= 15 ? -ext : c0 <= 21 ? 0 : ext;
    if (c0 > 2 && c0 <= 15 && c1 >= 22)
      shift = 0;                                      /* a centred message */
    for (int px = c0 * 8; px < (c1 + 1) * 8; px++) {
      int dest = px + shift + ext;
      if (dest >= 0 && dest < width)
        map[dest] = (int16_t)px;
    }
    c0 = c1 + 1;
  }
}

void render_frame_wide(int ext)
{
  const uint8_t *r = vdp.reg;
  int width = 320 + 2 * ext;
  render_wide_effects = 0;
  if (ext < 0 || width > RENDER_WIDE_MAX)
    return;
  unsigned bg = r[7] & 0x3f;
  if (!(r[1] & 0x40)) {
    uint32_t c = 0xff000000 | cram_rgb(vdp.cram[bg]);
    for (int y = 0; y < MD_MAX_H; y++)
      for (int i = 0; i < width; i++) {
        render_wide_back[y][i] = c;
        render_wide_plane_lo[y][i] = render_wide_plane_hi[y][i] = 0;
      }
    return;
  }
  static const unsigned sizes[4] = {32, 64, 64, 128};
  unsigned hcells = sizes[r[16] & 3], vcells = sizes[(r[16] >> 4) & 3];
  unsigned hmask = hcells * 8 - 1, vmask = vcells * 8 - 1;
  uint32_t nt_a = (uint32_t)(r[2] & 0x38) << 10;
  uint32_t nt_b = (uint32_t)(r[4] & 0x07) << 13;
  uint32_t nt_w = (uint32_t)(r[3] & 0x3c) << 10;
  uint32_t hs_table = (uint32_t)r[13] << 10;
  unsigned wh = r[17] & 0x1f, wv = r[18] & 0x1f;
  int full_rows = !wh;                                /* window lines cover the whole width */
  /* only a HUD at the top can be spread over a wide picture */
  if (wh || (r[18] & 0x80) || wv > 8)
    render_wide_effects++;
  if (full_rows)
    for (unsigned ty = 0; ty < 28; ty++)
      if ((r[18] & 0x80) ? ty >= wv : ty < wv)
        build_hud_map(nt_w, ty, ext, width);

  for (int y = 0; y < MD_MAX_H; y++) {
    unsigned hs_line = (r[11] & 3) == 3 ? (unsigned)y : (r[11] & 3) == 0 ? 0 : (unsigned)y & ~7u;
    uint16_t hs_a = vram_word(hs_table + hs_line * 4) & 0x3ff;
    uint16_t hs_b = vram_word(hs_table + hs_line * 4 + 2) & 0x3ff;
    uint16_t vs_a = vdp.line_vscroll[y][0], vs_b = vdp.line_vscroll[y][1];
    int win_line = (r[18] & 0x80) ? ((unsigned)y >= wv * 8) : ((unsigned)y < wv * 8);
    int win_x0 = 0, win_x1 = 0;
    if (!win_line && wh) {
      if (r[17] & 0x80) { win_x0 = (int)wh * 16; win_x1 = 320; }
      else { win_x0 = 0; win_x1 = (int)wh * 16; }
    }
    for (int i = 0; i < width; i++) {
      int x = i - ext;
      unsigned pa = 0;
      if (win_line && full_rows) {
        int sx = hud_map[y >> 3][i];
        if (sx >= 0) {
          uint16_t entry = vram_word(nt_w + ((unsigned)(y >> 3) * 64 + (unsigned)(sx >> 3)) * 2);
          pa = tile_pixel(entry, (unsigned)sx & 7, (unsigned)y & 7);
        }
      } else if ((win_line || (x >= win_x0 && x < win_x1)) && x >= 0 && x < 320) {
        uint16_t entry = vram_word(nt_w + ((unsigned)(y >> 3) * 64 + (unsigned)(x >> 3)) * 2);
        pa = tile_pixel(entry, (unsigned)x & 7, (unsigned)y & 7);
      } else {
        unsigned ax = ((unsigned)x - hs_a) & hmask, ay = ((unsigned)y + vs_a) & vmask;
        uint16_t entry = vram_word(nt_a + ((ay >> 3) * hcells + (ax >> 3)) * 2);
        pa = tile_pixel(entry, ax & 7, ay & 7);
        render_wide_effects += (pa & 0x10f) > 0x100 && x >= 0 && x < 320;
      }
      unsigned bx = ((unsigned)x - hs_b) & hmask, by = ((unsigned)y + vs_b) & vmask;
      unsigned pb = tile_pixel(vram_word(nt_b + ((by >> 3) * hcells + (bx >> 3)) * 2), bx & 7, by & 7);
      unsigned ac = pa & 0x3f, bc = pb & 0x3f;
      int a_hi = (pa >> 8) & 1, b_hi = (pb >> 8) & 1;
      render_wide_back[y][i] = 0xff000000 | cram_rgb(vdp.cram[(bc & 0x0f) ? bc : bg]);
      render_wide_effects += (bc & 0x0f) && b_hi;       /* would cover low priority sprites */
      int a_visible = (ac & 0x0f) && (a_hi || !((bc & 0x0f) && b_hi));
      uint32_t a_rgb = a_visible ? 0xff000000 | cram_rgb(vdp.cram[ac]) : 0;
      render_wide_plane_lo[y][i] = a_hi ? 0 : a_rgb;
      render_wide_plane_hi[y][i] = a_hi ? a_rgb : 0;
    }
  }
}

void render_frame(void)
{
  int width = (vdp.reg[12] & 0x81) ? 320 : 256;
  render_width = width;
  for (int y = 0; y < MD_MAX_H; y++)
    render_line(y, vdp.line_vscroll[y][0], vdp.line_vscroll[y][1], render_frame_rgb[y], width);
}

void render_state(StateIO *io)
{
  STATE_VAR(io, spr_ovr);
}
