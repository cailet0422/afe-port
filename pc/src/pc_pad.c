/* pc_pad.c - GC controller input via SDL gamepad + keyboard */
#include "pc_platform.h"
#include "pc_typing.h"
#include "pc_keybindings.h"
#include "pc_input_script.h"
#include "pc_touch.h"
#include "pc_ko.h"
#include "pc_minimap.h"
#include "pc_gui.h"
#include <dolphin/pad.h>

/* analog stick constants */
#define STICK_MAGNITUDE     80
#define AXIS_DEADZONE       4000
#define TRIGGER_THRESHOLD   100
#define RUMBLE_DURATION_MS  200

static SDL_GameController* g_controller = NULL;

/* Keys (1 << PC_BIND_*) that were down while they typed: they press nothing
 * until they are let go, so the Enter that finishes a text, or the Shift of
 * Shift+Enter, does not also press Start or B in the menu that comes next */
static u32 s_typed_held = 0;

#define BIT(bind) (1u << (bind))

/* Opens the first gamepad SDL can open (with none, g_controller stays) */
static void open_first_controller(void) {
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            g_controller = SDL_GameControllerOpen(i);
            if (g_controller) {
                break;
            }
        }
    }
}

BOOL PADInit(void) {
    open_first_controller();
    return TRUE;
}

/* A gamepad plugged in since the last read is opened, one pulled out closed */
static void update_controller(void) {
    if (!g_controller) {
        open_first_controller();
    }
    if (g_controller && !SDL_GameControllerGetAttached(g_controller)) {
        SDL_GameControllerClose(g_controller);
        g_controller = NULL;
    }
}

/* The GameCube button of each PC_BIND_* that is one */
static const u16 s_bind_buttons[PC_BIND_COUNT] = {
    [PC_BIND_A] = PAD_BUTTON_A,
    [PC_BIND_B] = PAD_BUTTON_B,
    [PC_BIND_X] = PAD_BUTTON_X,
    [PC_BIND_Y] = PAD_BUTTON_Y,
    [PC_BIND_START] = PAD_BUTTON_START,
    [PC_BIND_Z] = PAD_TRIGGER_Z,
    [PC_BIND_L] = PAD_TRIGGER_L,
    [PC_BIND_R] = PAD_TRIGGER_R,
    [PC_BIND_DPAD_UP] = PAD_BUTTON_UP,
    [PC_BIND_DPAD_DOWN] = PAD_BUTTON_DOWN,
    [PC_BIND_DPAD_LEFT] = PAD_BUTTON_LEFT,
    [PC_BIND_DPAD_RIGHT] = PAD_BUTTON_RIGHT,
};

static int pad_input_pressed(PCPadInput in) {
    if (in == PC_PAD_TRIGGER_LEFT || in == PC_PAD_TRIGGER_RIGHT) {
        s16 v = SDL_GameControllerGetAxis(g_controller, in == PC_PAD_TRIGGER_LEFT ? SDL_CONTROLLER_AXIS_TRIGGERLEFT
                                                                                 : SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        return (v >> 7) > TRIGGER_THRESHOLD;
    }
    return in >= 0 && in < SDL_CONTROLLER_BUTTON_MAX &&
           SDL_GameControllerGetButton(g_controller, (SDL_GameControllerButton)in);
}

/* Held controls to buttons and full stick tilts */
static void apply_held(u32 held, u16* buttons, s8* stickX, s8* stickY, s8* cstickX, s8* cstickY) {
    for (int i = 0; i < PC_BIND_COUNT; i++) {
        if (held & BIT(i)) {
            *buttons |= s_bind_buttons[i];
        }
    }
    if (held & BIT(PC_BIND_STICK_UP))     *stickY += STICK_MAGNITUDE;
    if (held & BIT(PC_BIND_STICK_DOWN))   *stickY -= STICK_MAGNITUDE;
    if (held & BIT(PC_BIND_STICK_LEFT))   *stickX -= STICK_MAGNITUDE;
    if (held & BIT(PC_BIND_STICK_RIGHT))  *stickX += STICK_MAGNITUDE;
    if (held & BIT(PC_BIND_CSTICK_UP))    *cstickY += STICK_MAGNITUDE;
    if (held & BIT(PC_BIND_CSTICK_DOWN))  *cstickY -= STICK_MAGNITUDE;
    if (held & BIT(PC_BIND_CSTICK_LEFT))  *cstickX -= STICK_MAGNITUDE;
    if (held & BIT(PC_BIND_CSTICK_RIGHT)) *cstickX += STICK_MAGNITUDE;
}

/* The controls held on the keyboard and the mouse (keybindings.ini /
 * Settings > Controls) */
static u32 keyboard_held(const u8* keys) {
    u32 mouse = SDL_GetMouseState(NULL, NULL);
    u32 held = 0;

    for (int i = 0; i < PC_BIND_COUNT; i++) {
        PCInputCode code = g_pc_keybindings.key[i];
        int down = code < 0                       ? 0
                   : (code & PC_INPUT_MOUSE_BIT) ? (mouse & SDL_BUTTON(code & 0xFF)) != 0
                                                 : keys[(SDL_Scancode)code] != 0;

        if (down) {
            held |= BIT(i);
        }
    }
    /* the Chat key opens the chat box from its key press (pc_gui.cpp), so
     * the letter it types is not typed into the box */
    held &= ~BIT(PC_BIND_CHAT);

    /* Ctrl+S saves (pc_main.c): S (the stick down by default) is left out
     * while Ctrl is held, and then until it is let go */
    if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) {
        for (int i = 0; i < PC_BIND_COUNT; i++) {
            if (g_pc_keybindings.key[i] == SDL_SCANCODE_S) {
                s_typed_held |= held & BIT(i);
            }
        }
    }
    return held;
}

/* Keys type into the in-game text editor while it takes input, or into the
 * chat box while it is open; once they finish, the keys still held are
 * ignored until let go (the Enter that sent a line is not Start). Also on
 * the first read after: the Enter that sent a chat line closes the box in
 * the frame it is pressed, before this read. */
static u32 drop_typed_keys(u32 held) {
    static int s_typing_before = 0;
    int editor = pc_typing_takes_keys();
    int typing = editor || pc_gui_chat_active();

    if (typing || s_typing_before) {
        /* In the game's text editor, controls on keys that type nothing
         * (Home, End, F1...) still press their buttons: Start on such a key
         * finishes the text, as on a gamepad. The chat box keeps them all
         * (Home and End move its cursor). */
        u32 pass = 0;

        if (editor) {
            for (int i = 0; i < PC_BIND_MAP; i++) { /* the pad's buttons and sticks, not the map */
                if ((held & BIT(i)) && !pc_typing_key_types(g_pc_keybindings.key[i])) {
                    pass |= BIT(i);
                }
            }
        }
        s_typed_held = held & ~pass;
        held = pass;
    } else {
        s_typed_held &= held;
        held &= ~s_typed_held;
    }
    s_typing_before = typing;
    return held;
}

/* The controls held on the gamepad */
static u32 gamepad_held(void) {
    u32 held = 0;

    for (int i = 0; i < PC_BIND_COUNT; i++) {
        for (int j = 0; j < PC_PAD_BINDS; j++) {
            if (pad_input_pressed(g_pc_keybindings.pad[i][j])) {
                held |= BIT(i);
            }
        }
    }
    return held;
}

/* An analog stick axis past the dead zone sets the stick (up is +y) */
static void axis_tilt(SDL_GameControllerAxis axis, int flip, s8* out) {
    s16 v = SDL_GameControllerGetAxis(g_controller, axis);

    if (abs(v) > AXIS_DEADZONE) {
        int s = flip ? -(v >> 8) : v >> 8;

        *out = (s8)(s > 127 ? 127 : s < -128 ? -128 : s);
    }
}

/* A gamepad button bound to Chat opens the chat box when pressed */
static void chat_button(int down) {
    static int s_chat_pad_held = 0;

    if (down && !s_chat_pad_held) {
        pc_gui_chat_request();
    }
    s_chat_pad_held = down;
}

u32 PADRead(PADStatus* status) {
    const u8* keys = SDL_GetKeyboardState(NULL);
    u16 buttons = 0;
    s8 stickX = 0, stickY = 0;
    s8 cstickX = 0, cstickY = 0;
    u32 held;    /* 1 << PC_BIND_* */
    u32 pad = 0; /* of them, on the gamepad */

    memset(status, 0, sizeof(PADStatus) * 4);

    held = drop_typed_keys(keyboard_held(keys));
    update_controller();
    if (g_controller) {
        pad = gamepad_held();
        held |= pad;
    }
    apply_held(held, &buttons, &stickX, &stickY, &cstickX, &cstickY);
    if (g_controller) {
        /* the analog sticks over the controls' full tilts */
        axis_tilt(SDL_CONTROLLER_AXIS_LEFTX, 0, &stickX);
        axis_tilt(SDL_CONTROLLER_AXIS_LEFTY, 1, &stickY);
        axis_tilt(SDL_CONTROLLER_AXIS_RIGHTX, 0, &cstickX);
        axis_tilt(SDL_CONTROLLER_AXIS_RIGHTY, 1, &cstickY);
        status[0].triggerLeft = (u8)(SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
        status[0].triggerRight = (u8)(SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);
    }
    chat_button((pad & BIT(PC_BIND_CHAT)) != 0);

    pc_touch_read(&buttons, &stickX, &stickY, &cstickX, &cstickY);
    pc_input_script_apply(&buttons, &stickX, &stickY);
    pc_minimap_pad((held & BIT(PC_BIND_MAP)) != 0, &buttons);
    pc_ko_osk_pad(&buttons, &stickX, &stickY);

    if (pc_gui_chat_active()) {
        /* typing in the chat box: the game gets a pad nobody touches */
        buttons = 0;
        stickX = stickY = cstickX = cstickY = 0;
        status[0].triggerLeft = status[0].triggerRight = 0;
    }
    status[0].button = buttons;
    status[0].stickX = stickX;
    status[0].stickY = stickY;
    status[0].substickX = cstickX;
    status[0].substickY = cstickY;
    status[0].err = 0; /* PAD_ERR_NONE */

    return PAD_CHAN0_BIT; /* Controller 1 connected */
}

void PADControlMotor(s32 chan, u32 command) {
    if (g_controller && chan == 0) {
        u16 intensity = (command == 1) ? 0xFFFF : 0;
        SDL_GameControllerRumble(g_controller, intensity, intensity, RUMBLE_DURATION_MS);
    }
}

void PADControlAllMotors(const u32* commands) {
    PADControlMotor(0, commands[0]);
}

void PADCleanup(void) {
    if (g_controller) {
        SDL_GameControllerClose(g_controller);
        g_controller = NULL;
    }
}

BOOL PADReset(u32 mask) { (void)mask; return TRUE; }
BOOL PADRecalibrate(u32 mask) { (void)mask; return TRUE; }
BOOL PADSync(void) { return TRUE; }
void PADSetSpec(u32 spec) { (void)spec; }
void PADSetAnalogMode(u32 mode) { (void)mode; }
/* PADClamp compiled from decomp: src/static/dolphin/pad/Padclamp.c */
BOOL PADGetType(s32 chan, u32* type) { if (type) *type = 0x09000000; return TRUE; }
