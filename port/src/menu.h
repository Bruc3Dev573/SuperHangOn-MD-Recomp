/*
 * Settings menu drawn on the UI layer. The game is paused while it is open.
 */
#ifndef MENU_H
#define MENU_H
#include <stdint.h>
#include "settings.h"

enum { MENU_OPEN, MENU_CLOSED, MENU_QUIT, MENU_RESET };

/* what a change needs from the frontend */
enum {
  APPLY_WINDOW = 1, APPLY_FULLSCREEN = 2, APPLY_VSYNC = 4,
  APPLY_FPS = 8, APPLY_SAVE = 16, APPLY_MUSIC = 32
};

void menu_open(void);
/* one frame of the menu with the pad state (bit0 U 1 D 2 L 3 R 4 B 5 C 6 A 7 S)
 * and whether "back" was pressed; returns MENU_*; *apply collects APPLY_* */
int menu_update(AppSettings *s, uint16_t pad, int back, int *apply);
void menu_draw(const AppSettings *s);

#endif
