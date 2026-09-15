/*
 * Keyboard and game controller input, mapped to the 3-button pad and to the
 * frontend's hotkeys through a configuration file.
 */
#ifndef INPUT_H
#define INPUT_H
#include <stddef.h>
#include <stdint.h>
#include <SDL.h>

enum {
  HOTKEY_QUIT, HOTKEY_FULLSCREEN, HOTKEY_SAVE, HOTKEY_LOAD, HOTKEY_SLOT_PREV,
  HOTKEY_SLOT_NEXT, HOTKEY_REWIND, HOTKEY_FPS, HOTKEY_MENU, HOTKEY_COUNT
};

enum {
  INPUT_ANALOG_THROTTLE = 1,
  INPUT_ANALOG_BRAKE = 2,
  INPUT_ANALOG_STEERING = 4
};

/* loads `path`, writing the default configuration there first if missing */
void input_init(const char *config_path);
void input_event(const SDL_Event *e);
/* pad bits: bit0 U 1 D 2 L 3 R 4 B 5 C 6 A 7 S */
uint16_t input_pad(void);
/* hotkey held now / pressed since the last call */
int input_hotkey_held(int hotkey);
int input_hotkey_pressed(int hotkey);

/* action names and bindings used by the in-game controls editor */
int input_action_count(void);
const char *input_action_name(int action);
void input_action_binding_text(int action, char *buf, size_t size);
void input_save(void);
void input_rebind_begin(int action);
void input_rebind_cancel(void);
int input_rebind_active(void);
int input_rebind_finished(void);
/* returns analog flags and values: throttle/brake 0..255, steering 0..255 */
int input_analog(uint8_t *throttle, uint8_t *brake, uint8_t *steering);

#endif
