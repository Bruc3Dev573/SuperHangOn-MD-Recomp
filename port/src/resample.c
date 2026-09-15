#include <math.h>
#include <string.h>
#include "resample.h"

#define HALF (RESAMPLE_TAPS / 2)

void resample_init(Resampler *r, double in_rate, double out_rate)
{
  memset(r, 0, sizeof *r);
  r->step = in_rate / out_rate;
  double nyquist = (in_rate < out_rate ? in_rate : out_rate) / 2;
  double fc = 0.9 * nyquist / in_rate;                   /* cycles per input sample */
  for (int p = 0; p <= RESAMPLE_PHASES; p++) {
    double frac = (double)p / RESAMPLE_PHASES, sum = 0;
    for (int k = 0; k < RESAMPLE_TAPS; k++) {
      double x = (k - HALF + 1) - frac;                  /* distance from the output time */
      double s = x == 0 ? 2 * fc : sin(2 * M_PI * fc * x) / (M_PI * x);
      double w = (x + HALF) / RESAMPLE_TAPS;             /* Blackman window over [-HALF, HALF] */
      double win = w <= 0 || w >= 1 ? 0 : 0.42 - 0.5 * cos(2 * M_PI * w) + 0.08 * cos(4 * M_PI * w);
      r->taps[p][k] = (float)(s * win);
      sum += s * win;
    }
    for (int k = 0; k < RESAMPLE_TAPS; k++)              /* unity gain at DC */
      r->taps[p][k] = (float)(r->taps[p][k] / sum);
  }
  r->pos = HALF - 1;
}

void resample_set_step(Resampler *r, double step)
{
  r->step = step;
}

size_t resample_run(Resampler *r, const int16_t *in, size_t frames, int16_t *out, size_t max_out)
{
  size_t written = 0;
  for (;;) {
    /* take input */
    size_t take = RESAMPLE_BUF - r->fill;
    if (take > frames)
      take = frames;
    for (size_t i = 0; i < take; i++) {
      r->buf[r->fill + i][0] = in[i * 2];
      r->buf[r->fill + i][1] = in[i * 2 + 1];
    }
    r->fill += take;
    in += take * 2;
    frames -= take;

    /* produce output while the filter window is inside the buffer */
    while (written < max_out && r->pos + HALF + 1 < (double)r->fill) {
      size_t i = (size_t)r->pos;
      double frac = (r->pos - (double)i) * RESAMPLE_PHASES;
      int p = (int)frac;
      float t = (float)(frac - p);
      const float *h0 = r->taps[p], *h1 = r->taps[p + 1];
      const float (*x)[2] = r->buf + i - (HALF - 1);
      float acc[2] = {0, 0};
      for (int k = 0; k < RESAMPLE_TAPS; k++) {
        float h = h0[k] + (h1[k] - h0[k]) * t;
        acc[0] += x[k][0] * h;
        acc[1] += x[k][1] * h;
      }
      for (int c = 0; c < 2; c++) {
        /* DC blocker, pole at ~5 Hz */
        float y = acc[c] - r->dc_x[c] + 0.9993f * r->dc_y[c];
        r->dc_x[c] = acc[c];
        r->dc_y[c] = y;
        long v = lrintf(y);
        out[written * 2 + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
      }
      written++;
      r->pos += r->step;
    }

    /* drop consumed input, keeping the filter's history */
    size_t keep_from = (size_t)r->pos >= HALF - 1 ? (size_t)r->pos - (HALF - 1) : 0;
    if (keep_from > r->fill)
      keep_from = r->fill;
    if (keep_from) {
      memmove(r->buf, r->buf + keep_from, (r->fill - keep_from) * sizeof r->buf[0]);
      r->fill -= keep_from;
      r->pos -= (double)keep_from;
    }
    if (frames == 0 || (take == 0 && written >= max_out))
      break;
  }
  return written;
}
