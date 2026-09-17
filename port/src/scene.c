#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "scene.h"
#include "md.h"
#include "render.h"

/* game RAM (offsets in the $FF0000 area) */
#define RAM_STEP_Y      0x0f00    /* w[192]: height of each depth step above the bottom line, 1/16 line */
#define RAM_CURVE       0x0d00    /* w[192]: road offset of each depth step, 1/64 pixel */
#define RAM_LINE_STEP   0x1300    /* w[224]: 2 * step + 1 shown on each line, 0 above the road */
#define RAM_STRIPE      0x0662    /* w: stripe phase (low 5 bits) */
#define ROM_STRIPES     0xfe6e    /* b[32 * 96]: per phase and step / 2, nonzero = second road copy */
#define STEPS           192
#define ROAD_ROW        320       /* plane B row of step 0 in the first road copy */
#define STRIPE_ROWS     192       /* the second copy is this many rows above */
#define MAX_RUNS        24

static uint8_t snap[0x10000];     /* RAM at the end of the last frame */
static int snap_valid;

void scene_frame_end(void)
{
  memcpy(snap, md.ram, sizeof snap);
  snap_valid = 1;
}

static int16_t snap_w(unsigned a)
{
  return (int16_t)(snap[a] << 8 | snap[a + 1]);
}

static uint16_t vram_w(unsigned a)
{
  return (uint16_t)(vdp.vram[a & 0xffff] << 8 | vdp.vram[(a + 1) & 0xffff]);
}

/* colour (palette << 4 | index) of a plane B pixel, unscrolled (64 x 64 cells) */
static unsigned plane_b_pixel(unsigned x, unsigned y)
{
  unsigned nt = (unsigned)(vdp.reg[4] & 7) << 13;
  uint16_t e = vram_w(nt + ((y >> 3) * 64 + (x >> 3)) * 2);
  unsigned px = x & 7, py = y & 7;
  if (e & 0x0800) px = 7 - px;
  if (e & 0x1000) py = 7 - py;
  uint8_t b = vdp.vram[(((e & 0x7ff) << 5) + (py << 2) + (px >> 1)) & 0xffff];
  return ((e >> 13) & 3) << 4 | ((px & 1) ? (b & 0x0f) : (b >> 4));
}

typedef struct {
  int n;                          /* runs */
  uint8_t colour[MAX_RUNS];
  float start[MAX_RUNS];          /* start of each run (start[0] = 0) */
} Row;

static void row_runs(unsigned y, Row *r)
{
  r->n = 0;
  unsigned prev = 0x100;
  for (unsigned x = 0; x < 512; x++) {
    unsigned c = plane_b_pixel(x, y);
    if (c != prev) {
      if (r->n == MAX_RUNS) { r->n = 0; return; }
      r->colour[r->n] = (uint8_t)c;
      r->start[r->n++] = (float)x;
      prev = c;
    }
  }
}

/* straight line per colour boundary of the road picture: x = a + b * step */
typedef struct {
  int n;
  uint8_t colour[2][MAX_RUNS];    /* [0] first copy, [1] second copy (stripes) */
  double a[MAX_RUNS], b[MAX_RUNS];
} RoadModel;

static int fit_road(RoadModel *m)
{
  /* the rows of steps 24-95 show both road edges on every course; nearer rows
   * are partly covered (the picture is wider than the plane there) */
  enum { K0 = 24, K1 = 96 };
  static Row rows[K1];
  int best = -1, best_count = 0;
  for (int k = K0; k < K1; k++)
    row_runs(ROAD_ROW + k, &rows[k]);
  for (int k = K0; k < K1; k++) {
    if (rows[k].n < 3) continue;
    int count = 0;
    for (int j = K0; j < K1; j++)
      count += rows[j].n == rows[k].n && !memcmp(rows[j].colour, rows[k].colour, rows[k].n);
    if (count >= 12 && (best < 0 || rows[k].n > rows[best].n || (rows[k].n == rows[best].n && count > best_count))) {
      best = k;
      best_count = count;
    }
  }
  if (best < 0)
    return 0;
  const Row *ref = &rows[best];
  m->n = ref->n;
  memcpy(m->colour[0], ref->colour, ref->n);
  Row upper;
  row_runs(ROAD_ROW - STRIPE_ROWS + best, &upper);
  if (upper.n != ref->n)
    return 0;
  memcpy(m->colour[1], upper.colour, upper.n);

  uint8_t use[K1];
  for (int k = 0; k < K1; k++)
    use[k] = k >= K0 && rows[k].n == ref->n && !memcmp(rows[k].colour, ref->colour, ref->n);
  for (int pass = 0; pass < 2; pass++) {
    for (int j = 1; j < m->n; j++) {
      double sn = 0, sk = 0, sx = 0, skk = 0, skx = 0;
      for (int k = K0; k < K1; k++)
        if (use[k]) {
          double x = rows[k].start[j];
          sn += 1; sk += k; sx += x; skk += (double)k * k; skx += k * x;
        }
      double det = sn * skk - sk * sk;
      if (sn < 8 || det == 0)
        return 0;
      m->b[j] = (sn * skx - sk * sx) / det;
      m->a[j] = (sx - m->b[j] * sk) / sn;
    }
    /* drop rows that do not follow the lines (covered or redrawn parts) */
    for (int k = K0; k < K1; k++)
      if (use[k])
        for (int j = 1; j < m->n; j++)
          if (fabs(rows[k].start[j] - (m->a[j] + m->b[j] * k)) > 1.5)
            use[k] = 0;
  }
  return 1;
}

/* which of the two road copies (palettes) every depth step is drawn with:
 * from the stripe phase, and for the steps on screen from the line scroll the
 * VDP displayed (after the goal the phase of the snapshot is one step ahead of
 * the picture) */
static uint8_t step_stripe[STEPS];

/* the line scroll values the game computes from the tables (sub_00EB86)
 * must be the ones the VDP displayed */
static int tables_match_display(void)
{
  unsigned hs_table = (unsigned)vdp.reg[13] << 10;
  unsigned phase = (unsigned)snap_w(RAM_STRIPE) & 31;
  for (int i = 0; i < STEPS; i++)
    step_stripe[i] = md.rom[ROM_STRIPES + phase * 96 + (i >> 1)] != 0;
  int road_lines = 0;
  for (int line = 0; line < 224; line++) {
    uint16_t v = (uint16_t)snap_w(RAM_LINE_STEP + 2 * line);
    if (!v)
      continue;
    unsigned step = v >> 1;
    if (step >= STEPS)
      return 0;
    uint16_t vs = (uint16_t)(0x140 - line + step);
    uint16_t hs = (uint16_t)((snap_w(RAM_CURVE + 2 * step) >> 6) - 100);
    uint16_t shown = vdp.line_vscroll[line][1];
    if ((vram_w(hs_table + line * 4 + 2) ^ hs) & 0x3ff)
      return 0;
    if (!((shown ^ vs) & 0x3ff))
      step_stripe[step] = 0;
    else if (!((shown ^ (uint16_t)(vs - STRIPE_ROWS)) & 0x3ff))
      step_stripe[step] = 1;
    else
      return 0;
    road_lines++;
  }
  return road_lines;
}

/* nearest depth step shown by the road at each 1/VIS_SUB line (-1: no road);
 * sprites behind hills are hidden where the road in front covers them */
#define VIS_SUB 8
static float visible_step[224 * VIS_SUB];

static void mark_visible(float ya, float yb, float step_a, float step_b)
{
  if (yb <= ya)
    return;
  int s0 = (int)ceil(ya * VIS_SUB - 0.5f), s1 = (int)ceil(yb * VIS_SUB - 0.5f);
  if (s0 < 0) s0 = 0;
  if (s1 > 224 * VIS_SUB) s1 = 224 * VIS_SUB;
  for (int i = s0; i < s1; i++) {
    float t = ((i + 0.5f) / VIS_SUB - ya) / (yb - ya);
    visible_step[i] = step_a + (step_b - step_a) * t;     /* drawn far to near: the last one is in front */
  }
}

static void emit(Scene *s, float y0, float y1, float x00, float x01, float x10, float x11, uint32_t rgb)
{
  if (s->nquads == SCENE_MAX_QUADS || (x01 <= x00 && x11 <= x10))
    return;
  SceneQuad *q = &s->quad[s->nquads++];
  q->y0 = y0; q->y1 = y1;
  q->x00 = x00; q->x01 = x01; q->x10 = x10; q->x11 = x11;
  q->rgb = rgb;
}

static int build_road(Scene *s)
{
  s->nquads = 0;
  if (!snap_valid || (vdp.reg[11] & 3) != 3 || (vdp.reg[16] & 0x33) != 0x11 || !(vdp.reg[1] & 0x40))
    return 0;
  if (!tables_match_display())
    return 0;
  static RoadModel m;
  if (!fit_road(&m))
    return 0;

  int top = snap_w(RAM_STEP_Y) >> 4;
  if (top < 0 || 223 - top < 0)
    return 0;
  for (int i = 0; i < 224 * VIS_SUB; i++)
    visible_step[i] = -1.0f;
  float y_prev = 0, x_prev[MAX_RUNS + 1];
  float centre_prev = 160.0f;
  for (int i = 0; i < STEPS; i++) {
    /* step i is displayed on the lines between the bottom edges of steps i
     * and i + 1 (sub_00EFA8 writes step i - 1 down to the line of step i) */
    float y = 224.0f - snap_w(RAM_STEP_Y + 2 * i) / 16.0f;
    float hs = snap_w(RAM_CURVE + 2 * i) / 64.0f - 100.0f;
    float centre = (float)((m.a[1] + m.a[m.n - 1]) + (m.b[1] + m.b[m.n - 1]) * i) * 0.5f + hs;
    /* the plane wraps every 512 pixels: the farthest step takes the copy
     * nearest the middle of the screen, the others follow the curve */
    float ref = i == 0 ? 160.0f : centre_prev;
    float wrap = 512.0f * (float)floor((ref - centre) / 512.0f + 0.5f);
    centre_prev = centre + wrap;
    float x[MAX_RUNS + 1];
    float left = -64.0f - s->ext, right = 384.0f + s->ext;
    x[0] = left;
    for (int j = 1; j < m.n; j++) {
      x[j] = (float)(m.a[j] + m.b[j] * i) + hs + wrap;
      if (x[j] < x[j - 1]) x[j] = x[j - 1];         /* lines meeting towards the horizon */
    }
    x[m.n] = right;
    for (int j = 1; j < m.n; j++) {
      if (x[j] < left) x[j] = left;
      if (x[j] > right) x[j] = right;
    }
    if (i > 0 && y != y_prev) {
      int stripe = step_stripe[i - 1];
      int up = y < y_prev;
      for (int r = 0; r < m.n; r++) {
        uint32_t rgb = render_colour(m.colour[stripe][r]);
        if (up)
          emit(s, y, y_prev, x[r], x[r + 1], x_prev[r], x_prev[r + 1], rgb);
        else
          emit(s, y_prev, y, x_prev[r], x_prev[r + 1], x[r], x[r + 1], rgb);
      }
      if (up)
        mark_visible(y, y_prev, (float)i, (float)(i - 1));
      else
        mark_visible(y_prev, y, (float)(i - 1), (float)i);
    }
    y_prev = y;
    memcpy(x_prev, x, sizeof x);
  }
  return s->nquads > 0;
}

/* ---- sprites ---------------------------------------------------------- */

#define OBJ_FIRST        0xc900
#define OBJ_COUNT        29
#define OBJ_SIZE         0x40
#define TYPE_SCENERY     0x34     /* roadside objects (loc_00C782) */
#define MAX_PIECES       32
#define IMG_MAX          640      /* pixels per side of a composed mapping */
#define PIECE_REACH      400      /* pieces farther from the anchor are never on screen */
#define PIECE_KEY        0x10000  /* image keys of single sprites: PIECE_KEY | size, attr = tile */

static uint16_t rom_w(unsigned a)
{
  return (uint16_t)(md.rom[a] << 8 | md.rom[a + 1]);
}

static uint16_t ram_w(unsigned a)
{
  return (uint16_t)(snap[a & 0xffff] << 8 | snap[(a + 1) & 0xffff]);
}

typedef struct { int16_t y; uint8_t size; uint16_t tile; int16_t x; } Piece;

/* sprite table entries of an object as built by sub_00716E (pieces outside
 * X $5F-$1BE are left out); *total: pieces of its mapping */
static int object_pieces(unsigned obj, Piece *out, int *total)
{
  unsigned map = ram_w(obj + 0x24);
  *total = 0;
  if (!map || map >= 0x8000)
    return 0;
  int count = rom_w(map) + 1, n = 0;
  if (count > MAX_PIECES)
    return -1;
  uint16_t attr = ram_w(obj + 2), ox = ram_w(obj + 0x10), oy = ram_w(obj + 0x14);
  for (int i = 0; i < count; i++) {
    unsigned p = map + 2 + (unsigned)i * 6;
    int16_t x = (int16_t)(rom_w(p + 4) + ox);
    if (x >= 0x1bf || x < 0x5f)
      continue;
    out[n].y = (int16_t)((0xff00 | md.rom[p]) + oy);
    out[n].size = md.rom[p + 1];
    out[n].tile = (uint16_t)(rom_w(p + 2) + attr);
    out[n].x = x;
    n++;
  }
  *total = count;
  return n;
}

/* a mapping (or a single sprite) composed from VRAM tiles: 0xAARRGGBB,
 * anchor at (ox, oy) */
typedef struct {
  unsigned key;
  uint16_t attr;
  uint64_t hash;
  int id, w, h, ox, oy;
  uint32_t *pixels;
  int capacity;
  int last_used;
} Image;

#define CACHE_SIZE 256
static Image *cache[CACHE_SIZE];
static int next_id = 1, frame_counter;

static void draw_piece(Image *img, int dx, int dy, int size, uint16_t tile)
{
  int tw = ((size >> 2) & 3) + 1, th = (size & 3) + 1;
  for (int py = 0; py < th * 8; py++)
    for (int px = 0; px < tw * 8; px++) {
      int tx = (tile & 0x0800) ? tw * 8 - 1 - px : px;
      int ty = (tile & 0x1000) ? th * 8 - 1 - py : py;
      unsigned t = (tile & 0x7ff) + (unsigned)((tx >> 3) * th + (ty >> 3));
      uint8_t b = vdp.vram[((t << 5) + (unsigned)((ty & 7) << 2) + (unsigned)((tx & 7) >> 1)) & 0xffff];
      unsigned c = (tx & 1) ? (b & 0x0f) : (b >> 4);
      if (c)
        img->pixels[(dy + py) * img->w + dx + px] = 0xff000000 | render_colour(((tile >> 13) & 3) << 4 | c);
    }
}

static int alloc_pixels(Image *img, int w, int h)
{
  if (img->capacity < w * h) {
    free(img->pixels);
    img->capacity = 0;
    if (!(img->pixels = malloc((size_t)w * h * sizeof img->pixels[0])))
      return 0;
    img->capacity = w * h;
  }
  img->w = w;
  img->h = h;
  memset(img->pixels, 0, (size_t)w * h * sizeof img->pixels[0]);
  return 1;
}

static int compose(unsigned key, uint16_t attr, Image *img)
{
  if (key & PIECE_KEY) {
    int size = key & 0xff;
    img->ox = img->oy = 0;
    if (!alloc_pixels(img, (((size >> 2) & 3) + 1) * 8, ((size & 3) + 1) * 8))
      return 0;
    draw_piece(img, 0, 0, size, attr);
    return 1;
  }
  unsigned map = key;
  if (!map || map >= 0x8000)
    return 0;
  int count = rom_w(map) + 1;
  if (count > MAX_PIECES)
    return 0;
  int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
  for (int i = 0; i < count; i++) {
    unsigned p = map + 2 + (unsigned)i * 6;
    int dx = (int16_t)rom_w(p + 4), dy = (int)md.rom[p] - 256, sz = md.rom[p + 1];
    if (dx < -PIECE_REACH || dx > PIECE_REACH)
      continue;
    if (dx < x0) x0 = dx;
    if (dy < y0) y0 = dy;
    if (dx + (((sz >> 2) & 3) + 1) * 8 > x1) x1 = dx + (((sz >> 2) & 3) + 1) * 8;
    if (dy + ((sz & 3) + 1) * 8 > y1) y1 = dy + ((sz & 3) + 1) * 8;
  }
  if (x1 <= x0 || x1 - x0 > IMG_MAX || y1 - y0 > IMG_MAX)
    return 0;
  img->ox = -x0;
  img->oy = -y0;
  if (!alloc_pixels(img, x1 - x0, y1 - y0))
    return 0;
  for (int i = count - 1; i >= 0; i--) {             /* the first piece is in front */
    unsigned p = map + 2 + (unsigned)i * 6;
    int dx = (int16_t)rom_w(p + 4);
    if (dx < -PIECE_REACH || dx > PIECE_REACH)
      continue;
    draw_piece(img, dx - x0, (int)md.rom[p] - 256 - y0, md.rom[p + 1], (uint16_t)(rom_w(p + 2) + attr));
  }
  return 1;
}

static uint64_t hash_pixels(const Image *img)
{
  uint64_t h = 1469598103934665603ull ^ (uint64_t)img->w << 32 ^ (uint64_t)img->h;
  for (int i = 0; i < img->w * img->h; i++)
    h = (h ^ img->pixels[i]) * 1099511628211ull;
  return h;
}

/* composed image, from the cache when its pixels did not change */
static Image *image_for(unsigned key, uint16_t attr)
{
  static Image tmp;
  if (!compose(key, attr, &tmp))
    return NULL;
  tmp.hash = hash_pixels(&tmp);
  Image *slot = NULL;
  for (int i = 0; i < CACHE_SIZE; i++) {
    Image *c = cache[i];
    if (c && c->key == key && c->attr == attr) {
      if (c->hash == tmp.hash) {
        c->last_used = frame_counter;
        return c;
      }
      slot = c;
      break;
    }
  }
  if (!slot) {
    int victim = 0;
    for (int i = 0; i < CACHE_SIZE; i++) {
      if (!cache[i]) { victim = i; break; }
      if (cache[i]->last_used < cache[victim]->last_used) victim = i;
    }
    if (!cache[victim] && !(cache[victim] = calloc(1, sizeof(Image))))
      return NULL;
    slot = cache[victim];
  }
  if (!alloc_pixels(slot, tmp.w, tmp.h))
    return NULL;
  memcpy(slot->pixels, tmp.pixels, (size_t)tmp.w * tmp.h * sizeof tmp.pixels[0]);
  slot->ox = tmp.ox;
  slot->oy = tmp.oy;
  slot->hash = tmp.hash;
  slot->key = key;
  slot->attr = attr;
  slot->id = next_id++;
  slot->last_used = frame_counter;
  return slot;
}

static int add_image(Scene *s, const Image *img)
{
  for (int i = 0; i < s->nimages; i++)
    if (s->image[i].id == img->id)
      return i;
  if (s->nimages == SCENE_MAX_IMAGES)
    return -1;
  SceneImage *h = &s->image[s->nimages];
  h->id = img->id;
  h->w = img->w;
  h->h = img->h;
  h->pixels = img->pixels;
  return s->nimages++;
}

/* adds an image anchored at (ax, ay) for an object at depth `step` (< 0:
 * never hidden): it is cut where the road shows a nearer step, everywhere if
 * its base is covered, else only above the base (so that shadows below it
 * stay) */
static int add_sprite(Scene *s, const Image *img, int ax, int ay, int step)
{
  int index = add_image(s, img);
  if (index < 0)
    return 0;
  float x0 = (float)(ax - img->ox), y0 = (float)(ay - img->oy);
  float x1 = x0 + img->w, y1 = y0 + img->h;
  /* beside the 4:3 screen the road is not drawn, so a hill cannot hide an
   * object there: what a hill cuts would float over the flat background */
  int beside = x1 <= 0.0f || x0 >= 320.0f;
  int first = s->nsprites;
  const float margin = 1.5f;
  int base = ay * VIS_SUB;
  int base_hidden = step >= 0 && base >= 0 && base < 224 * VIS_SUB && visible_step[base] > step + margin;
  int i0 = (int)floor(y0 * VIS_SUB), i1 = (int)ceil(y1 * VIS_SUB);
  float start = y0;
  int open = 1;                                       /* inside a visible run */
  for (int i = i0; i <= i1; i++) {
    float y = (float)i / VIS_SUB;
    int hidden = 0;
    if (i < i1 && step >= 0 && i >= 0 && i < 224 * VIS_SUB)
      hidden = visible_step[i] > step + margin && (base_hidden || y + 1.0f / VIS_SUB <= ay - 1.0f);
    if (i == i1 || hidden) {
      float end = i == i1 ? y1 : (y > y0 ? y : y0);
      if (open && end > start) {
        if (beside && (start > y0 || end < y1)) {
          s->nsprites = first;                        /* cut by a hill: not drawn */
          return 1;
        }
        if (s->nsprites == SCENE_MAX_SPRITES)
          return 0;
        SceneSprite *sp = &s->sprite[s->nsprites++];
        sp->x0 = x0; sp->x1 = x1;
        sp->y0 = start; sp->y1 = end;
        sp->v0 = (start - y0) / (y1 - y0);
        sp->v1 = (end - y0) / (y1 - y0);
        sp->image = index;
      }
      open = 0;
    } else if (!open) {
      open = 1;
      start = y;
    }
  }
  return 1;
}

/* what is drawn, in sprite table order: an object slot, or a single table
 * entry */
typedef struct { int object, entry; } Item;

static int sprite_matches(unsigned sat, int entry, const Piece *pieces, int np)
{
  for (int j = 0; j < np; j++) {
    unsigned e = sat + (unsigned)(entry + j) * 8;
    if (((vram_w(e) ^ (uint16_t)pieces[j].y) & 0x3ff) || vdp.vram[(e + 2) & 0xffff] != pieces[j].size ||
        vram_w(e + 4) != pieces[j].tile || ((vram_w(e + 6) ^ (uint16_t)pieces[j].x) & 0x3ff))
      return 0;
  }
  return 1;
}

/* objects the table does not show: culled pieces, or roadside objects beside
 * the 4:3 screen that the game does not queue (loc_00C7F8) */
static int drawn_off_table(unsigned obj, int np, int total)
{
  int x = (int16_t)ram_w(obj + 0x10);
  return total && (!np || ((ram_w(obj) & 0xfffe) == TYPE_SCENERY && (x <= 0x40 || x >= 0x200)));
}

static int build_sprites(Scene *s)
{
  s->nsprites = 0;
  s->nimages = 0;
  frame_counter++;

  /* sprite table order: objects by depth bucket (nearest first), then slot */
  int order[OBJ_COUNT], n = 0;
  for (int i = 0; i < OBJ_COUNT; i++) {
    unsigned obj = OBJ_FIRST + (unsigned)i * OBJ_SIZE;
    int bucket = ram_w(obj + 0x0c) & 0xfffe;
    if (!(ram_w(obj) & 0xfffe) || !ram_w(obj + 0x24) || bucket > 192)
      continue;
    int k = n++;
    while (k > 0 && (ram_w(OBJ_FIRST + (unsigned)order[k - 1] * OBJ_SIZE + 0x0c) & 0xfffe) < bucket) {
      order[k] = order[k - 1];
      k--;
    }
    order[k] = i;
  }

  /* the displayed table: linked in order */
  unsigned sat = (unsigned)(vdp.reg[5] & 0x7e) << 9;
  int entries = 0;
  for (;;) {
    int link = vdp.vram[(sat + (unsigned)entries * 8 + 3) & 0xffff];
    entries++;
    if (!link || entries == 80)
      break;
    if (link != entries)
      return 0;
  }
  if (entries == 1 && (vram_w(sat) & 0x1ff) == 0x168)
    entries = 0;                                      /* no sprites */

  /* walk the table: each entry starts the next object that matches there,
   * or is a single sprite */
  static Item items[OBJ_COUNT + 80];
  int nitems = 0, entry = 0, k = 0;
  Piece pieces[MAX_PIECES];
  while (entry < entries) {
    int found = -1, found_np = 0;
    for (int kk = k; kk < n && found < 0; kk++) {
      int total, np = object_pieces(OBJ_FIRST + (unsigned)order[kk] * OBJ_SIZE, pieces, &total);
      if (np > entries - entry)
        np = entries - entry;                         /* the table holds 80 sprites */
      if (np > 0 && sprite_matches(sat, entry, pieces, np)) {
        found = kk;
        found_np = np;
      }
    }
    if (found < 0) {
      items[nitems++] = (Item){-1, entry++};
      continue;
    }
    for (; k < found; k++) {
      unsigned obj = OBJ_FIRST + (unsigned)order[k] * OBJ_SIZE;
      int total, np = object_pieces(obj, pieces, &total);
      if (drawn_off_table(obj, np, total))
        items[nitems++] = (Item){order[k], -1};
    }
    items[nitems++] = (Item){order[found], -1};
    entry += found_np;
    k = found + 1;
  }
  for (; k < n; k++) {
    unsigned obj = OBJ_FIRST + (unsigned)order[k] * OBJ_SIZE;
    int total, np = object_pieces(obj, pieces, &total);
    if (drawn_off_table(obj, np, total))
      items[nitems++] = (Item){order[k], -1};
  }

  /* far to near */
  for (int i = nitems - 1; i >= 0; i--) {
    if (items[i].object < 0) {
      unsigned e = sat + (unsigned)items[i].entry * 8;
      uint16_t tile = vram_w(e + 4);
      const Image *img = image_for(PIECE_KEY | vdp.vram[(e + 2) & 0xffff], tile);
      if (!img || !add_sprite(s, img, (vram_w(e + 6) & 0x1ff) - 128, (vram_w(e) & 0x1ff) - 128, -1))
        return 0;
      continue;
    }
    unsigned obj = OBJ_FIRST + (unsigned)items[i].object * OBJ_SIZE;
    const Image *img = image_for(ram_w(obj + 0x24), ram_w(obj + 2));
    int step = (int16_t)ram_w(obj + 0x0c);
    if (!img || !add_sprite(s, img, (int16_t)ram_w(obj + 0x10) - 128, (int16_t)ram_w(obj + 0x14) - 128,
                            step >= 1 && step < STEPS ? step : -1))
      return 0;
  }
  return 1;
}

int scene_build(Scene *s, int ext)
{
  s->ext = ext;
  s->nsprites = s->nimages = 0;
  if (render_wide_effects || !build_road(s))
    return 0;
  return build_sprites(s);
}

/* ---- software rasteriser (tests) ---------------------------------------- */

static void blend_layer(uint32_t *buf, int sw, int sh, const uint32_t layer[][RENDER_WIDE_MAX], int width)
{
  for (int y = 0; y < sh; y++)
    for (int x = 0; x < sw; x++) {
      uint32_t c = layer[y * 224 / sh][x * width / sw];
      if (c >> 24)
        buf[y * sw + x] = c & 0xffffff;
    }
}

void scene_rasterize(const Scene *s, uint32_t *buf, int scale)
{
  int width = 320 + 2 * s->ext, sw = width * scale, sh = 224 * scale;
  for (int y = 0; y < sh; y++)
    for (int x = 0; x < sw; x++)
      buf[y * sw + x] = render_wide_back[y * 224 / sh][x * width / sw] & 0xffffff;
  float f = (float)scale, ox = (float)s->ext;
  for (int n = 0; n < s->nquads; n++) {
    const SceneQuad *q = &s->quad[n];
    int ya = (int)ceil(q->y0 * f - 0.5f), yb = (int)ceil(q->y1 * f - 0.5f);
    for (int y = ya < 0 ? 0 : ya; y < yb && y < sh; y++) {
      float t = ((y + 0.5f) / f - q->y0) / (q->y1 - q->y0);
      float xa = (q->x00 + (q->x10 - q->x00) * t + ox) * f, xb = (q->x01 + (q->x11 - q->x01) * t + ox) * f;
      int ia = (int)ceil(xa - 0.5f), ib = (int)ceil(xb - 0.5f);
      if (ia < 0) ia = 0;
      if (ib > sw) ib = sw;
      for (int x = ia; x < ib; x++)
        buf[y * sw + x] = q->rgb;
    }
  }
  blend_layer(buf, sw, sh, render_wide_plane_lo, width);
  for (int n = 0; n < s->nsprites; n++) {
    const SceneSprite *sp = &s->sprite[n];
    const SceneImage *im = &s->image[sp->image];
    int xa = (int)ceil((sp->x0 + ox) * f - 0.5f), xb = (int)ceil((sp->x1 + ox) * f - 0.5f);
    int ya = (int)ceil(sp->y0 * f - 0.5f), yb = (int)ceil(sp->y1 * f - 0.5f);
    for (int y = ya < 0 ? 0 : ya; y < yb && y < sh; y++)
      for (int x = xa < 0 ? 0 : xa; x < xb && x < sw; x++) {
        int u = (int)(((x + 0.5f) / f - ox - sp->x0) / (sp->x1 - sp->x0) * im->w);
        int v = (int)((sp->v0 + ((y + 0.5f) / f - sp->y0) / (sp->y1 - sp->y0) * (sp->v1 - sp->v0)) * im->h);
        if (u < 0 || v < 0 || u >= im->w || v >= im->h) continue;
        uint32_t c = im->pixels[v * im->w + u];
        if (c >> 24)
          buf[y * sw + x] = c & 0xffffff;
      }
  }
  blend_layer(buf, sw, sh, render_wide_plane_hi, width);
}
