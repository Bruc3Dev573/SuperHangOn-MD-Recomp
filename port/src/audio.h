/*
 * Sound hardware: Z80 running the game's original sound driver, YM2612
 * (Nuked-OPN2) and SN76489 PSG, clocked in master-clock cycles.
 */
#ifndef AUDIO_H
#define AUDIO_H
#include <stddef.h>
#include <stdint.h>

#define MD_MASTER_CLOCK 53693175u
#define MD_MCYCLES_PER_LINE 3420u
#define MD_LINES_PER_FRAME 262u
#define AUDIO_NATIVE_RATE ((double)MD_MASTER_CLOCK / 1008.0)   /* YM2612 sample rate, ~53267 Hz */
#define AUDIO_MUSIC_TRACK_COUNT 4

void audio_init(void);
/* keep the sound hardware and driver on a 60 Hz wall clock when the game
 * simulation is running at 120 Hz */
void audio_set_game_fps(int fps);
/* advance sound hardware for this game's frame-clock interval */
void audio_run(uint32_t mcycles);
/* Z80 /INT line (asserted by the VDP at vertical blank) */
void audio_set_z80_int(int asserted);
/* Z80 reset line asserted by the 68000: resets the Z80 and the YM2612 */
void audio_z80_reset(void);
/* starts one of the four original music tracks (1..4 in the menu) */
void audio_play_music(int track);
/* master-clock time of the sound hardware since power on */
uint64_t audio_mcycles(void);
/* native-rate stereo samples produced so far (interleaved L/R); returns frames */
size_t audio_read(int16_t *dst, size_t max_frames);

/* optional logging of chip writes (verification against the reference) */
extern void (*audio_ym_log)(unsigned port, unsigned value);
extern void (*audio_psg_log)(unsigned value);

#endif
