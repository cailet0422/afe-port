#ifndef PC_KEYBINDINGS_H
#define PC_KEYBINDINGS_H

#include <stddef.h>
#include <SDL_scancode.h>
#include <SDL_mouse.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PCInputCode: values 0..SDL_NUM_SCANCODES-1 are keyboard scancodes.
   Values with PC_INPUT_MOUSE_BIT set are mouse buttons (low bits = SDL button index). */
typedef int PCInputCode;

#define PC_INPUT_NONE        (-1)
#define PC_INPUT_MOUSE_BIT   0x10000
#define PC_INPUT_MOUSE1      (PC_INPUT_MOUSE_BIT | SDL_BUTTON_LEFT)    /* left click */
#define PC_INPUT_MOUSE2      (PC_INPUT_MOUSE_BIT | SDL_BUTTON_RIGHT)   /* right click */
#define PC_INPUT_MOUSE3      (PC_INPUT_MOUSE_BIT | SDL_BUTTON_MIDDLE)  /* middle click */

/* PCPadInput: an SDL_GameControllerButton, or a trigger pulled past halfway.
   The left stick always drives the main stick and the right stick the C-stick. */
typedef int PCPadInput;

#define PC_PAD_NONE          (-1)
#define PC_PAD_TRIGGER_LEFT  0x100
#define PC_PAD_TRIGGER_RIGHT 0x101
#define PC_PAD_BINDS         2 /* gamepad inputs per action (Start = start, back) */

/* GameCube controls, in the order of keybindings.ini and the settings menu */
enum {
    PC_BIND_A,
    PC_BIND_B,
    PC_BIND_X,
    PC_BIND_Y,
    PC_BIND_START,
    PC_BIND_Z,
    PC_BIND_L,
    PC_BIND_R,
    PC_BIND_STICK_UP,
    PC_BIND_STICK_DOWN,
    PC_BIND_STICK_LEFT,
    PC_BIND_STICK_RIGHT,
    PC_BIND_CSTICK_UP,
    PC_BIND_CSTICK_DOWN,
    PC_BIND_CSTICK_LEFT,
    PC_BIND_CSTICK_RIGHT,
    PC_BIND_DPAD_UP,
    PC_BIND_DPAD_DOWN,
    PC_BIND_DPAD_LEFT,
    PC_BIND_DPAD_RIGHT,
    PC_BIND_COUNT
};

typedef struct {
    PCInputCode key[PC_BIND_COUNT];
    PCPadInput pad[PC_BIND_COUNT][PC_PAD_BINDS];
} PCKeybindings;

extern PCKeybindings g_pc_keybindings;

void pc_keybindings_load(void);
/* Writes keybindings.ini (both sections) */
void pc_keybindings_save(void);
/* Back to the defaults (not saved) */
void pc_keybindings_reset(void);

/* For the settings menu (ASCII): "Stick Up", "Left Shift", "LB, LT" */
const char* pc_keybindings_action_name(int action);
void pc_keybindings_key_label(int action, char* buf, size_t n);
void pc_keybindings_pad_label(int action, char* buf, size_t n);

/* Bind a key or mouse button / one gamepad input to action. Another action
   that had it takes over action's previous one, so nothing ends up bound
   twice and nothing silently loses its control. */
void pc_keybindings_set_key(int action, PCInputCode code);
void pc_keybindings_set_pad(int action, PCPadInput input);

#ifdef __cplusplus
}
#endif

#endif /* PC_KEYBINDINGS_H */
