/* pc_pad.c - GC controller input via SDL gamepad + keyboard */
#include "pc_platform.h"
#include "pc_typing.h"
#include "pc_keybindings.h"
#include "pc_input_script.h"
#include "pc_touch.h"
#include <dolphin/pad.h>

/* analog stick constants */
#define STICK_MAGNITUDE     80
#define AXIS_DEADZONE       4000
#define TRIGGER_THRESHOLD   100
#define RUMBLE_DURATION_MS  200

static SDL_GameController* g_controller = NULL;

BOOL PADInit(void) {
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            g_controller = SDL_GameControllerOpen(i);
            if (g_controller) {
                break;
            }
        }
    }
    return TRUE;
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
        if (held & (1u << i)) {
            *buttons |= s_bind_buttons[i];
        }
    }
    if (held & (1u << PC_BIND_STICK_UP))     *stickY += STICK_MAGNITUDE;
    if (held & (1u << PC_BIND_STICK_DOWN))   *stickY -= STICK_MAGNITUDE;
    if (held & (1u << PC_BIND_STICK_LEFT))   *stickX -= STICK_MAGNITUDE;
    if (held & (1u << PC_BIND_STICK_RIGHT))  *stickX += STICK_MAGNITUDE;
    if (held & (1u << PC_BIND_CSTICK_UP))    *cstickY += STICK_MAGNITUDE;
    if (held & (1u << PC_BIND_CSTICK_DOWN))  *cstickY -= STICK_MAGNITUDE;
    if (held & (1u << PC_BIND_CSTICK_LEFT))  *cstickX -= STICK_MAGNITUDE;
    if (held & (1u << PC_BIND_CSTICK_RIGHT)) *cstickX += STICK_MAGNITUDE;
}

u32 PADRead(PADStatus* status) {
    memset(status, 0, sizeof(PADStatus) * 4);

    const u8* keys = SDL_GetKeyboardState(NULL);
    u32 mouse = SDL_GetMouseState(NULL, NULL);
    u16 buttons = 0;
    u32 held = 0; /* 1 << PC_BIND_* */
    s8 stickX = 0, stickY = 0;
    s8 cstickX = 0, cstickY = 0;

    /* Suppress keyboard-to-button mapping when typing into the in-game text editor */
    if (!(g_pc_typing_mode && g_pc_editor_active)) {
        /* helper: check if a PCInputCode is currently pressed */
        #define INPUT_PRESSED(code) \
            ((code) < 0 ? 0 \
             : ((code) & PC_INPUT_MOUSE_BIT) \
                ? (mouse & SDL_BUTTON((code) & 0xFF)) \
                : keys[(SDL_Scancode)(code)])

        /* from keybindings.ini / Settings > Controls */
        for (int i = 0; i < PC_BIND_COUNT; i++) {
            if (INPUT_PRESSED(g_pc_keybindings.key[i])) {
                held |= 1u << i;
            }
        }

        #undef INPUT_PRESSED
    }

    /* hotplug */
    if (!g_controller) {
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (SDL_IsGameController(i)) {
                g_controller = SDL_GameControllerOpen(i);
                if (g_controller) break;
            }
        }
    }

    if (g_controller) {
        if (!SDL_GameControllerGetAttached(g_controller)) {
            SDL_GameControllerClose(g_controller);
            g_controller = NULL;
        }
    }
    if (g_controller) {
        for (int i = 0; i < PC_BIND_COUNT; i++) {
            for (int j = 0; j < PC_PAD_BINDS; j++) {
                if (pad_input_pressed(g_pc_keybindings.pad[i][j])) {
                    held |= 1u << i;
                }
            }
        }

        apply_held(held, &buttons, &stickX, &stickY, &cstickX, &cstickY);
        held = 0;

        s16 lx = SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_LEFTX);
        s16 ly = SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_LEFTY);
        if (abs(lx) > AXIS_DEADZONE) {
            int sx = lx >> 8;
            if (sx > 127) sx = 127; else if (sx < -128) sx = -128;
            stickX = (s8)sx;
        }
        if (abs(ly) > AXIS_DEADZONE) {
            int sy = -(ly >> 8);
            if (sy > 127) sy = 127; else if (sy < -128) sy = -128;
            stickY = (s8)sy;
        }

        s16 rx = SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_RIGHTX);
        s16 ry = SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_RIGHTY);
        if (abs(rx) > AXIS_DEADZONE) {
            int srx = rx >> 8;
            if (srx > 127) srx = 127; else if (srx < -128) srx = -128;
            cstickX = (s8)srx;
        }
        if (abs(ry) > AXIS_DEADZONE) {
            int sry = -(ry >> 8);
            if (sry > 127) sry = 127; else if (sry < -128) sry = -128;
            cstickY = (s8)sry;
        }

        u8 lt = (u8)(SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
        u8 rt = (u8)(SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);
        status[0].triggerLeft = lt;
        status[0].triggerRight = rt;
    }

    apply_held(held, &buttons, &stickX, &stickY, &cstickX, &cstickY);

    pc_touch_read(&buttons, &stickX, &stickY);
    pc_input_script_apply(&buttons, &stickX, &stickY);

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
