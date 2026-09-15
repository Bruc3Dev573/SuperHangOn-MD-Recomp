#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "input.h"

#define MAX_BINDINGS 8
#define AXIS_THRESHOLD 16000

/* one physical input: keyboard scancode, controller button or controller axis
 * direction (axes: sign selects the direction, triggers are positive) */
typedef struct {
  enum { BIND_KEY, BIND_BUTTON, BIND_AXIS } kind;
  int code, sign;
} Binding;

typedef struct {
  const char *name;
  Binding bind[MAX_BINDINGS];
  int count;
  int pressed_latch;
} Action;

/* the 8 pad buttons in bit order, then the hotkeys */
static Action actions[8 + HOTKEY_COUNT] = {
  {"up"}, {"down"}, {"left"}, {"right"}, {"b"}, {"c"}, {"a"}, {"start"},
  {"quit"}, {"fullscreen"}, {"save_state"}, {"load_state"}, {"slot_prev"}, {"slot_next"}, {"rewind"}, {"fps_counter"}, {"menu"},
};
#define N_ACTIONS (int)(sizeof actions / sizeof actions[0])

static SDL_GameController *controller;
static char config_path[1024];
static int rebind_action = -1;
static int rebind_finished;
static void open_controller(int index);

static const char default_config[] =
  "# Super Hang-On PC port: controls\n"
  "#\n"
  "# action = input, input, ...\n"
  "#   key:NAME      SDL key name (key:Up, key:Z, key:Return, key:F5 ...)\n"
  "#   button:NAME   game controller button (a b x y back start leftshoulder\n"
  "#                 rightshoulder leftstick rightstick dpup dpdown dpleft dpright)\n"
  "#   axis:NAME+ / axis:NAME-   controller axis direction (leftx lefty rightx\n"
  "#                 righty; lefttrigger+ / righttrigger+ for the triggers)\n"
  "#\n"
  "# In the game's default control setting B accelerates, A brakes, C is turbo.\n"
  "up = key:Up, button:dpup, axis:lefty-\n"
  "down = key:Down, button:dpdown, axis:lefty+\n"
  "left = key:Left, button:dpleft, axis:leftx-\n"
  "right = key:Right, button:dpright, axis:leftx+\n"
  "a = key:Z, button:x, axis:lefttrigger+\n"
  "b = key:X, button:a, axis:righttrigger+\n"
  "c = key:C, button:b\n"
  "start = key:Return, button:start\n"
  "\n"
  "quit =\n"
  "fullscreen = key:F11\n"
  "save_state = key:F5\n"
  "load_state = key:F8\n"
  "slot_prev = key:F6\n"
  "slot_next = key:F7\n"
  "rewind = key:Backspace, button:leftshoulder\n"
  "fps_counter = key:F3\n"
  "menu = key:Escape, key:F1, button:back\n";

static char *trim(char *s)
{
  while (isspace((unsigned char)*s)) s++;
  char *e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
  return s;
}

static int parse_binding(const char *text, Binding *b)
{
  if (!strncmp(text, "key:", 4)) {
    SDL_Scancode sc = SDL_GetScancodeFromName(text + 4);
    if (sc == SDL_SCANCODE_UNKNOWN) return -1;
    b->kind = BIND_KEY;
    b->code = sc;
    return 0;
  }
  if (!strncmp(text, "button:", 7)) {
    SDL_GameControllerButton bt = SDL_GameControllerGetButtonFromString(text + 7);
    if (bt == SDL_CONTROLLER_BUTTON_INVALID) return -1;
    b->kind = BIND_BUTTON;
    b->code = bt;
    return 0;
  }
  if (!strncmp(text, "axis:", 5)) {
    char name[32];
    size_t n = strlen(text + 5);
    if (n < 2 || n >= sizeof name) return -1;
    memcpy(name, text + 5, n - 1);
    name[n - 1] = 0;
    char sign = text[5 + n - 1];
    SDL_GameControllerAxis ax = SDL_GameControllerGetAxisFromString(name);
    if (ax == SDL_CONTROLLER_AXIS_INVALID || (sign != '+' && sign != '-')) return -1;
    b->kind = BIND_AXIS;
    b->code = ax;
    b->sign = sign == '+' ? 1 : -1;
    return 0;
  }
  return -1;
}

static void load_config(const char *text, const char *path)
{
  char line[512];
  int lineno = 0;
  while (*text) {
    const char *nl = strchr(text, '\n');
    size_t len = nl ? (size_t)(nl - text) : strlen(text);
    if (len >= sizeof line) len = sizeof line - 1;
    memcpy(line, text, len);
    line[len] = 0;
    text = nl ? nl + 1 : text + strlen(text);
    lineno++;
    char *hash = strchr(line, '#');
    if (hash) *hash = 0;
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char *name = trim(line);
    int a;
    for (a = 0; a < N_ACTIONS; a++)
      if (!strcmp(actions[a].name, name)) break;
    if (a == N_ACTIONS) {
      fprintf(stderr, "%s:%d: unknown action '%s'\n", path, lineno, name);
      continue;
    }
    actions[a].count = 0;                        /* the file replaces the defaults */
    for (char *tok = strtok(eq + 1, ","); tok; tok = strtok(NULL, ",")) {
      char *t = trim(tok);
      if (!*t) continue;
      if (actions[a].count == MAX_BINDINGS || parse_binding(t, &actions[a].bind[actions[a].count]))
        fprintf(stderr, "%s:%d: ignored input '%s'\n", path, lineno, t);
      else
        actions[a].count++;
    }
  }
}

void input_init(const char *path)
{
  snprintf(config_path, sizeof config_path, "%s", path ? path : "");
  rebind_action = -1;
  rebind_finished = 0;
  /* defaults first, then the user's file */
  load_config(default_config, "defaults");
  if (path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
      FILE *w = fopen(path, "w");
      if (w) {
        fputs(default_config, w);
        fclose(w);
      }
    } else {
      fseek(f, 0, SEEK_END);
      long n = ftell(f);
      fseek(f, 0, SEEK_SET);
      char *text = n > 0 ? malloc((size_t)n + 1) : NULL;
      if (text) {
        text[fread(text, 1, (size_t)n, f)] = 0;
        load_config(text, path);
        free(text);
      }
      fclose(f);
    }
  }
  SDL_GameControllerEventState(SDL_ENABLE);
  for (int i = 0; i < SDL_NumJoysticks(); i++)
    open_controller(i);
}


static void open_controller(int index)
{
  if (!controller && SDL_IsGameController(index))
    controller = SDL_GameControllerOpen(index);
}

static void binding_text(const Binding *b, char *buf, size_t size)
{
  if (!size) return;
  switch (b->kind) {
    case BIND_KEY:
      snprintf(buf, size, "key:%s", SDL_GetScancodeName((SDL_Scancode)b->code));
      break;
    case BIND_BUTTON:
      snprintf(buf, size, "button:%s", SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b->code));
      break;
    case BIND_AXIS:
      snprintf(buf, size, "axis:%s%c",
               SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)b->code),
               b->sign > 0 ? '+' : '-');
      break;
  }
}

void input_save(void)
{
  if (!config_path[0])
    return;
  FILE *f = fopen(config_path, "w");
  if (!f)
    return;
  fputs("# Super Hang-On PC port: controls (written by the settings menu)\n", f);
  for (int a = 0; a < N_ACTIONS; a++) {
    fprintf(f, "%s =", actions[a].name);
    for (int i = 0; i < actions[a].count; i++) {
      char text[64];
      binding_text(&actions[a].bind[i], text, sizeof text);
      fprintf(f, "%s%s", i ? ", " : " ", text);
    }
    fputc('\n', f);
  }
  fclose(f);
}

int input_action_count(void)
{
  return N_ACTIONS;
}

const char *input_action_name(int action)
{
  return action >= 0 && action < N_ACTIONS ? actions[action].name : "";
}

void input_action_binding_text(int action, char *buf, size_t size)
{
  if (!size) return;
  buf[0] = 0;
  if (action < 0 || action >= N_ACTIONS)
    return;
  size_t used = 0;
  for (int i = 0; i < actions[action].count; i++) {
    char text[64];
    binding_text(&actions[action].bind[i], text, sizeof text);
    int n = snprintf(buf + used, size - used, "%s%s", used ? ", " : "", text);
    if (n < 0 || (size_t)n >= size - used) {
      buf[size - 1] = 0;
      return;
    }
    used += (size_t)n;
  }
  if (!used)
    snprintf(buf, size, "unbound");
}

static int binding_active(const Binding *b)
{
  switch (b->kind) {
    case BIND_KEY:
      return SDL_GetKeyboardState(NULL)[b->code] != 0;
    case BIND_BUTTON:
      return controller && SDL_GameControllerGetButton(controller, b->code);
    case BIND_AXIS:
      return controller && SDL_GameControllerGetAxis(controller, b->code) * b->sign > AXIS_THRESHOLD;
  }
  return 0;
}

static int action_active(const Action *a)
{
  for (int i = 0; i < a->count; i++)
    if (binding_active(&a->bind[i]))
      return 1;
  return 0;
}

static int binding_strength(const Binding *b, int *axis_available)
{
  if (b->kind != BIND_AXIS)
    return binding_active(b) ? 255 : 0;
  if (!controller)
    return 0;
  *axis_available = 1;
  int value = SDL_GameControllerGetAxis(controller, b->code) * b->sign;
  if (value <= AXIS_THRESHOLD)
    return 0;
  int strength = (value - AXIS_THRESHOLD) * 255 / (32767 - AXIS_THRESHOLD);
  return strength > 255 ? 255 : strength;
}

static int action_strength(const Action *a, int *axis_available)
{
  int strength = 0;
  for (int i = 0; i < a->count; i++) {
    int value = binding_strength(&a->bind[i], axis_available);
    if (value > strength)
      strength = value;
  }
  return strength;
}

int input_analog(uint8_t *throttle, uint8_t *brake, uint8_t *steering)
{
  int flags = 0;
  int throttle_axis = 0, brake_axis = 0, left_axis = 0, right_axis = 0;
  int left = action_strength(&actions[2], &left_axis);
  int right = action_strength(&actions[3], &right_axis);
  int t = action_strength(&actions[4], &throttle_axis);
  int b = action_strength(&actions[6], &brake_axis);
  int s = 128;
  if (right > left)
    s = 128 + right * 127 / 255;
  else if (left > right)
    s = 128 - left * 128 / 255;
  if (throttle) *throttle = (uint8_t)t;
  if (brake) *brake = (uint8_t)b;
  if (steering) *steering = (uint8_t)s;
  if (throttle_axis) flags |= INPUT_ANALOG_THROTTLE;
  if (brake_axis) flags |= INPUT_ANALOG_BRAKE;
  if (left_axis || right_axis) flags |= INPUT_ANALOG_STEERING;
  return flags;
}

uint16_t input_pad(void)
{
  uint16_t m = 0;
  for (int i = 0; i < 8; i++)
    if (action_active(&actions[i]))
      m |= 1u << i;
  if ((m & 0x03) == 0x03) m &= ~0x03;            /* opposite directions cancel */
  if ((m & 0x0c) == 0x0c) m &= ~0x0c;
  return m;
}

int input_hotkey_held(int hotkey)
{
  return rebind_action < 0 && !rebind_finished && action_active(&actions[8 + hotkey]);
}

int input_hotkey_pressed(int hotkey)
{
  Action *a = &actions[8 + hotkey];
  if (rebind_action >= 0 || rebind_finished)
    return 0;
  int now = action_active(a), pressed = now && !a->pressed_latch;
  a->pressed_latch = now;
  return pressed;
}

static void finish_rebind(const Binding *b)
{
  Action *a = &actions[rebind_action];
  a->count = 1;
  a->bind[0] = *b;
  rebind_action = -1;
  rebind_finished = 1;
  input_save();
}

int input_rebind_event(const SDL_Event *e)
{
  if (rebind_action < 0)
    return 0;
  if (e->type == SDL_KEYDOWN && !e->key.repeat) {
    if (e->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
      rebind_action = -1;
      rebind_finished = 1;
      return 1;
    }
    Binding b = {BIND_KEY, e->key.keysym.scancode, 0};
    finish_rebind(&b);
    return 1;
  }
  if (e->type == SDL_CONTROLLERBUTTONDOWN) {
    if (e->cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
      rebind_action = -1;
      rebind_finished = 1;
      return 1;
    }
    Binding b = {BIND_BUTTON, e->cbutton.button, 0};
    finish_rebind(&b);
    return 1;
  }
  if (e->type == SDL_CONTROLLERAXISMOTION && abs(e->caxis.value) > AXIS_THRESHOLD) {
    Binding b = {BIND_AXIS, e->caxis.axis, e->caxis.value > 0 ? 1 : -1};
    if (e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
        e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
      b.sign = 1;
    finish_rebind(&b);
    return 1;
  }
  if (e->type == SDL_KEYUP || e->type == SDL_CONTROLLERBUTTONUP ||
      e->type == SDL_CONTROLLERAXISMOTION)
    return 1;                                  /* don't leak capture input */
  return 0;
}

void input_rebind_begin(int action)
{
  if (action >= 0 && action < N_ACTIONS) {
    rebind_action = action;
    rebind_finished = 0;
  }
}

void input_rebind_cancel(void)
{
  rebind_action = -1;
  rebind_finished = 0;
}

int input_rebind_active(void)
{
  return rebind_action >= 0;
}

int input_rebind_finished(void)
{
  int finished = rebind_finished;
  rebind_finished = 0;
  return finished;
}

void input_event(const SDL_Event *e)
{
  if (e->type == SDL_CONTROLLERDEVICEADDED) {
    open_controller(e->cdevice.which);
  } else if (e->type == SDL_CONTROLLERDEVICEREMOVED && controller &&
             SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == e->cdevice.which) {
    SDL_GameControllerClose(controller);
    controller = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
      open_controller(i);
  }
  if (input_rebind_event(e))
    return;
}
