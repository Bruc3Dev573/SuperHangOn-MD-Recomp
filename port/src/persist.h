/*
 * Things kept outside the game: save state slots, the rewind buffer and the
 * game's records (best scores and ranking tables) between sessions.
 */
#ifndef PERSIST_H
#define PERSIST_H
#include <stddef.h>
#include "m68k_rt.h"

/* dir: user data directory ending with a path separator */
void persist_init(const char *dir);

/* call at the end of every frame (rt_frame_end_callback); performs the
 * requested actions and may resume the game elsewhere (state loaded) */
void persist_frame_end(M68K *c, int save, int load, int rewind);

/* restart the game at the next frame end, from the machine state of the
 * first frame (records stay: they are brought back when the game has
 * initialised again) */
void persist_request_reset(void);

int persist_slot(void);
void persist_set_slot(int slot);            /* 0-9 */
/* last message for the user ("State 3 saved"), cleared when read */
const char *persist_message(void);

#endif
