/*
 * Stereo sample-rate converter (windowed sinc, polyphase) with an adjustable
 * ratio, used to play the native ~53 kHz sound at the device rate while the
 * ratio follows the audio queue level (dynamic rate control).
 */
#ifndef RESAMPLE_H
#define RESAMPLE_H
#include <stddef.h>
#include <stdint.h>

#define RESAMPLE_TAPS 32
#define RESAMPLE_PHASES 256
#define RESAMPLE_BUF 16384

typedef struct {
  double step;                                  /* input frames per output frame */
  double pos;                                   /* input position of the next output frame */
  float taps[RESAMPLE_PHASES + 1][RESAMPLE_TAPS];
  float buf[RESAMPLE_BUF][2];                   /* pending input frames */
  size_t fill;
  float dc_x[2], dc_y[2];                       /* DC blocker state */
} Resampler;

/* nominal rates; the pass band ends at 0.9 of the lower Nyquist frequency */
void resample_init(Resampler *r, double in_rate, double out_rate);
void resample_set_step(Resampler *r, double step);
/* converts `frames` input frames, writing at most `max_out` output frames;
 * returns the number of output frames written */
size_t resample_run(Resampler *r, const int16_t *in, size_t frames, int16_t *out, size_t max_out);

#endif
