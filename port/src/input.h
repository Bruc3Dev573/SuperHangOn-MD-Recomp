/*
 * Keyboard and game controller input, mapped to the 3-button pad and to the
 * frontend's hotkeys through a configuration file.
 */
#ifndef INPUT_H
#define INPUT_H
#include <stdint.h>
#include <SDL.h>

enum {
  HOTKEY_QUIT, HOTKEY_FULLSCREEN, HOTKEY_SAVE, HOTKEY_LOAD, HOTKEY_SLOT_PREV,
  HOTKEY_SLOT_NEXT, HOTKEY_REWIND, HOTKEY_FPS, HOTKEY_MENU, HOTKEY_COUNT
};

/* loads `path`, writing the default configuration there first if missing */
void input_init(const char *config_path);
void input_event(const SDL_Event *e);
/* pad bits: bit0 U 1 D 2 L 3 R 4 B 5 C 6 A 7 S */
uint16_t input_pad(void);
/* analog steering: -127 (left) .. 127 (right), from the controller axis set
 * in the configuration (past the dead zone) or full when the direction
 * buttons or keys are held; flags bit 2: the steering is analog */
typedef struct {
  uint8_t flags;
  int8_t steer;
} AnalogInput;
void input_analog(AnalogInput *a);
/* hotkey held now / pressed since the last call */
int input_hotkey_held(int hotkey);
int input_hotkey_pressed(int hotkey);

#endif
