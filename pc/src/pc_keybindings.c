#include "pc_keybindings.h"
#include "pc_platform.h"

static const PCInputCode s_default_keys[PC_BIND_COUNT] = {
    /* buttons */
    [PC_BIND_A]     = SDL_SCANCODE_SPACE,
    [PC_BIND_B]     = SDL_SCANCODE_LSHIFT,
    [PC_BIND_X]     = SDL_SCANCODE_X,
    [PC_BIND_Y]     = SDL_SCANCODE_Y,
    [PC_BIND_START] = SDL_SCANCODE_RETURN,
    [PC_BIND_Z]     = SDL_SCANCODE_Z,
    [PC_BIND_L]     = SDL_SCANCODE_Q,
    [PC_BIND_R]     = SDL_SCANCODE_E,

    /* main stick (WASD) */
    [PC_BIND_STICK_UP]    = SDL_SCANCODE_W,
    [PC_BIND_STICK_DOWN]  = SDL_SCANCODE_S,
    [PC_BIND_STICK_LEFT]  = SDL_SCANCODE_A,
    [PC_BIND_STICK_RIGHT] = SDL_SCANCODE_D,

    /* C-stick (arrows) */
    [PC_BIND_CSTICK_UP]    = SDL_SCANCODE_UP,
    [PC_BIND_CSTICK_DOWN]  = SDL_SCANCODE_DOWN,
    [PC_BIND_CSTICK_LEFT]  = SDL_SCANCODE_LEFT,
    [PC_BIND_CSTICK_RIGHT] = SDL_SCANCODE_RIGHT,

    /* D-pad (IJKL) */
    [PC_BIND_DPAD_UP]    = SDL_SCANCODE_I,
    [PC_BIND_DPAD_DOWN]  = SDL_SCANCODE_K,
    [PC_BIND_DPAD_LEFT]  = SDL_SCANCODE_J,
    [PC_BIND_DPAD_RIGHT] = SDL_SCANCODE_L,

    [PC_BIND_MAP] = SDL_SCANCODE_M,
};

/* Positions as on an Xbox pad (SDL's GameController layout) */
static const PCPadInput s_default_pad[PC_BIND_COUNT][PC_PAD_BINDS] = {
    [PC_BIND_A]     = { SDL_CONTROLLER_BUTTON_A, PC_PAD_NONE },
    [PC_BIND_B]     = { SDL_CONTROLLER_BUTTON_B, PC_PAD_NONE },
    [PC_BIND_X]     = { SDL_CONTROLLER_BUTTON_X, PC_PAD_NONE },
    [PC_BIND_Y]     = { SDL_CONTROLLER_BUTTON_Y, PC_PAD_NONE },
    [PC_BIND_START] = { SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK },
    [PC_BIND_Z]     = { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PC_PAD_NONE },
    [PC_BIND_L]     = { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PC_PAD_TRIGGER_LEFT },
    [PC_BIND_R]     = { PC_PAD_TRIGGER_RIGHT, PC_PAD_NONE },

    /* the sticks move with the analog sticks; buttons can be added */
    [PC_BIND_STICK_UP]     = { PC_PAD_NONE, PC_PAD_NONE },
    [PC_BIND_STICK_DOWN]   = { PC_PAD_NONE, PC_PAD_NONE },
    [PC_BIND_STICK_LEFT]   = { PC_PAD_NONE, PC_PAD_NONE },
    [PC_BIND_STICK_RIGHT]  = { PC_PAD_NONE, PC_PAD_NONE },
    [PC_BIND_CSTICK_UP]    = { PC_PAD_NONE, PC_PAD_NONE },
    [PC_BIND_CSTICK_DOWN]  = { PC_PAD_NONE, PC_PAD_NONE },
    [PC_BIND_CSTICK_LEFT]  = { PC_PAD_NONE, PC_PAD_NONE },
    [PC_BIND_CSTICK_RIGHT] = { PC_PAD_NONE, PC_PAD_NONE },

    [PC_BIND_DPAD_UP]    = { SDL_CONTROLLER_BUTTON_DPAD_UP, PC_PAD_NONE },
    [PC_BIND_DPAD_DOWN]  = { SDL_CONTROLLER_BUTTON_DPAD_DOWN, PC_PAD_NONE },
    [PC_BIND_DPAD_LEFT]  = { SDL_CONTROLLER_BUTTON_DPAD_LEFT, PC_PAD_NONE },
    [PC_BIND_DPAD_RIGHT] = { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PC_PAD_NONE },

    [PC_BIND_MAP] = { PC_PAD_NONE, PC_PAD_NONE },
};

PCKeybindings g_pc_keybindings;
static int s_ready = 0;

static const char* KEYBINDINGS_FILE = "keybindings.ini";

/* ini key names and menu names, by action */
static const char* const s_ini_keys[PC_BIND_COUNT] = {
    "A", "B", "X", "Y", "Start", "Z", "L", "R",
    "Stick_Up", "Stick_Down", "Stick_Left", "Stick_Right",
    "CStick_Up", "CStick_Down", "CStick_Left", "CStick_Right",
    "DPad_Up", "DPad_Down", "DPad_Left", "DPad_Right",
    "Map",
};

static const char* const s_menu_names[PC_BIND_COUNT] = {
    "A", "B", "X", "Y", "Start", "Z", "L", "R",
    "Stick Up", "Stick Down", "Stick Left", "Stick Right",
    "C-Stick Up", "C-Stick Down", "C-Stick Left", "C-Stick Right",
    "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
    "Map",
};

/* mouse button name table */
typedef struct {
    const char* name;
    PCInputCode code;
} MouseButtonEntry;

static const MouseButtonEntry s_mouse_buttons[] = {
    { "Mouse1", PC_INPUT_MOUSE1 },
    { "Mouse2", PC_INPUT_MOUSE2 },
    { "Mouse3", PC_INPUT_MOUSE3 },
};
#define NUM_MOUSE_BUTTONS (sizeof(s_mouse_buttons) / sizeof(s_mouse_buttons[0]))

/* Short names for the menu */
static const char* const s_pad_labels[SDL_CONTROLLER_BUTTON_MAX] = {
    [SDL_CONTROLLER_BUTTON_A] = "A",
    [SDL_CONTROLLER_BUTTON_B] = "B",
    [SDL_CONTROLLER_BUTTON_X] = "X",
    [SDL_CONTROLLER_BUTTON_Y] = "Y",
    [SDL_CONTROLLER_BUTTON_BACK] = "Back",
    [SDL_CONTROLLER_BUTTON_GUIDE] = "Guide",
    [SDL_CONTROLLER_BUTTON_START] = "Start",
    [SDL_CONTROLLER_BUTTON_LEFTSTICK] = "LS",
    [SDL_CONTROLLER_BUTTON_RIGHTSTICK] = "RS",
    [SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = "LB",
    [SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = "RB",
    [SDL_CONTROLLER_BUTTON_DPAD_UP] = "D-Up",
    [SDL_CONTROLLER_BUTTON_DPAD_DOWN] = "D-Down",
    [SDL_CONTROLLER_BUTTON_DPAD_LEFT] = "D-Left",
    [SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = "D-Right",
};

static const char* skip_ws(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void trim_end(char* s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                       s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
}

/* get display name for an input code */
static const char* input_code_name(PCInputCode code) {
    if (code < 0) {
        return "none";
    }
    if (code & PC_INPUT_MOUSE_BIT) {
        for (int i = 0; i < (int)NUM_MOUSE_BUTTONS; i++) {
            if (s_mouse_buttons[i].code == code)
                return s_mouse_buttons[i].name;
        }
        return "Unknown";
    }
    return SDL_GetScancodeName((SDL_Scancode)code);
}

/* parse an input value string to a PCInputCode; -2 = invalid */
static PCInputCode parse_input_code(const char* value) {
    if (SDL_strcasecmp(value, "none") == 0) {
        return PC_INPUT_NONE;
    }

    /* check mouse button names first (case-insensitive) */
    for (int i = 0; i < (int)NUM_MOUSE_BUTTONS; i++) {
        if (SDL_strcasecmp(value, s_mouse_buttons[i].name) == 0) {
            return s_mouse_buttons[i].code;
        }
    }

    /* fall back to SDL scancode */
    SDL_Scancode sc = SDL_GetScancodeFromName(value);
    if (sc != SDL_SCANCODE_UNKNOWN) {
        return (PCInputCode)sc;
    }

    return -2; /* invalid */
}

/* SDL's names, as in game controller mappings */
static const char* pad_input_name(PCPadInput in) {
    if (in == PC_PAD_TRIGGER_LEFT) return SDL_GameControllerGetStringForAxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    if (in == PC_PAD_TRIGGER_RIGHT) return SDL_GameControllerGetStringForAxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    if (in >= 0 && in < SDL_CONTROLLER_BUTTON_MAX) {
        const char* s = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)in);
        if (s != NULL) return s;
    }
    return "none";
}

/* -2 = invalid */
static PCPadInput parse_pad_input(const char* value) {
    SDL_GameControllerButton button;
    SDL_GameControllerAxis axis;

    if (SDL_strcasecmp(value, "none") == 0) {
        return PC_PAD_NONE;
    }
    button = SDL_GameControllerGetButtonFromString(value);
    if (button != SDL_CONTROLLER_BUTTON_INVALID) {
        return (PCPadInput)button;
    }
    axis = SDL_GameControllerGetAxisFromString(value);
    if (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) return PC_PAD_TRIGGER_LEFT;
    if (axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) return PC_PAD_TRIGGER_RIGHT;
    return -2;
}

static int find_action(const char* key) {
    for (int i = 0; i < PC_BIND_COUNT; i++) {
        if (strcmp(key, s_ini_keys[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static void apply_keybind(const char* key, const char* value) {
    int action = find_action(key);
    PCInputCode code = parse_input_code(value);

    if (action < 0) {
        printf("[Keybindings] WARNING: unknown binding '%s'\n", key);
        return;
    }
    if (code == -2) {
        printf("[Keybindings] WARNING: unknown key name '%s' for '%s'\n", value, key);
        return;
    }
    g_pc_keybindings.key[action] = code;
}

/* "leftshoulder, lefttrigger" */
static void apply_padbind(const char* key, char* value) {
    int action = find_action(key);
    PCPadInput inputs[PC_PAD_BINDS] = { PC_PAD_NONE, PC_PAD_NONE };
    int count = 0;
    char* item = value;

    if (action < 0) {
        printf("[Keybindings] WARNING: unknown binding '%s'\n", key);
        return;
    }
    while (item != NULL) {
        char* comma = strchr(item, ',');
        PCPadInput in;

        if (comma != NULL) *comma = '\0';
        item = (char*)skip_ws(item);
        trim_end(item);
        if (*item != '\0') {
            in = parse_pad_input(item);
            if (in == -2) {
                printf("[Keybindings] WARNING: unknown gamepad input '%s' for '%s'\n", item, key);
                return;
            }
            if (in != PC_PAD_NONE && count < PC_PAD_BINDS) {
                inputs[count++] = in;
            }
        }
        item = comma != NULL ? comma + 1 : NULL;
    }
    memcpy(g_pc_keybindings.pad[action], inputs, sizeof(inputs));
}

/* A control added after a keybindings.ini was written starts with its
   default, which that file may already give to another control (M to A):
   then the new one starts unbound instead of pressing both */
static void unbind_taken_defaults(void) {
    static const int added[] = { PC_BIND_MAP };

    for (int a = 0; a < (int)(sizeof(added) / sizeof(added[0])); a++) {
        int action = added[a];

        for (int i = 0; i < PC_BIND_COUNT; i++) {
            if (i == action) continue;
            if (g_pc_keybindings.key[action] >= 0 && g_pc_keybindings.key[i] == g_pc_keybindings.key[action]) {
                printf("[Keybindings] %s: %s is taken, left unbound\n", s_ini_keys[action],
                       input_code_name(g_pc_keybindings.key[action]));
                g_pc_keybindings.key[action] = PC_INPUT_NONE;
            }
            for (int j = 0; j < PC_PAD_BINDS; j++) {
                for (int k = 0; k < PC_PAD_BINDS; k++) {
                    if (g_pc_keybindings.pad[action][j] != PC_PAD_NONE &&
                        g_pc_keybindings.pad[i][k] == g_pc_keybindings.pad[action][j]) {
                        g_pc_keybindings.pad[action][j] = PC_PAD_NONE;
                    }
                }
            }
        }
    }
}

void pc_keybindings_reset(void) {
    memcpy(g_pc_keybindings.key, s_default_keys, sizeof(s_default_keys));
    memcpy(g_pc_keybindings.pad, s_default_pad, sizeof(s_default_pad));
    s_ready = 1;
}

void pc_keybindings_save(void) {
    FILE* f = fopen(KEYBINDINGS_FILE, "w");
    if (!f) {
        printf("[Keybindings] Failed to write %s\n", KEYBINDINGS_FILE);
        return;
    }

    fprintf(f, "# Pause menu > Settings > Controls changes these too.\n");
    fprintf(f, "\n");
    fprintf(f, "[Keyboard]\n");
    fprintf(f, "# Key names use SDL2 scancode names.\n");
    fprintf(f, "# Common names: Space, Left Shift, Right Shift, Left Ctrl, Right Ctrl,\n");
    fprintf(f, "#   Left Alt, Right Alt, Return, Escape, Tab, Backspace, Delete,\n");
    fprintf(f, "#   A-Z, 0-9, F1-F12, Up, Down, Left, Right, etc.\n");
    fprintf(f, "# Mouse buttons: Mouse1 (left), Mouse2 (right), Mouse3 (middle). none = unbound\n");
    fprintf(f, "# Full list: https://wiki.libsdl.org/SDL2/SDL_Scancode\n");
    fprintf(f, "\n");
    fprintf(f, "# Buttons\n");

    for (int i = 0; i < PC_BIND_COUNT; i++) {
        fprintf(f, "%s = %s\n", s_ini_keys[i], input_code_name(g_pc_keybindings.key[i]));

        /* blank line separators between sections */
        if (i == PC_BIND_R)            fprintf(f, "\n# Main Stick\n");
        if (i == PC_BIND_STICK_RIGHT)  fprintf(f, "\n# C-Stick (Camera)\n");
        if (i == PC_BIND_CSTICK_RIGHT) fprintf(f, "\n# D-Pad\n");
        if (i == PC_BIND_DPAD_RIGHT)   fprintf(f, "\n# Opens and closes the town map at any time\n");
    }

    fprintf(f, "\n");
    fprintf(f, "[Gamepad]\n");
    fprintf(f, "# Buttons by position on an Xbox-style pad: a (bottom), b (right), x (left), y (top),\n");
    fprintf(f, "#   back, guide, start, leftstick, rightstick, leftshoulder, rightshoulder,\n");
    fprintf(f, "#   dpup, dpdown, dpleft, dpright; triggers: lefttrigger, righttrigger. none = unbound\n");
    fprintf(f, "# Up to two per line, separated by a comma. The left stick always moves the main\n");
    fprintf(f, "# stick and the right stick the C-stick; buttons given for them work as well.\n");
    fprintf(f, "\n");
    for (int i = 0; i < PC_BIND_COUNT; i++) {
        const PCPadInput* in = g_pc_keybindings.pad[i];

        if (in[1] != PC_PAD_NONE) {
            fprintf(f, "%s = %s, %s\n", s_ini_keys[i], pad_input_name(in[0]), pad_input_name(in[1]));
        } else {
            fprintf(f, "%s = %s\n", s_ini_keys[i], pad_input_name(in[0]));
        }
    }

    fclose(f);
    printf("[Keybindings] Saved %s\n", KEYBINDINGS_FILE);
}

void pc_keybindings_load(void) {
    pc_keybindings_reset();

    FILE* f = fopen(KEYBINDINGS_FILE, "r");
    if (!f) {
        pc_keybindings_save();
        printf("[Keybindings] Created default %s\n", KEYBINDINGS_FILE);
        return;
    }

    /* Lines before any section are keyboard keys (older files) */
    int gamepad = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        const char* p = skip_ws(line);

        if (*p == '[') {
            gamepad = SDL_strncasecmp(p, "[Gamepad]", 9) == 0;
            continue;
        }
        if (*p == '#' || *p == ';' || *p == '\0' || *p == '\n')
            continue;

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = (char*)skip_ws(line);
        trim_end(key);
        char* value = (char*)skip_ws(eq + 1);
        trim_end(value);

        if (*key && *value) {
            if (gamepad) {
                apply_padbind(key, value);
            } else {
                apply_keybind(key, value);
            }
        }
    }
    fclose(f);
    unbind_taken_defaults();
    printf("[Keybindings] Loaded %s\n", KEYBINDINGS_FILE);
}

const char* pc_keybindings_action_name(int action) {
    return (action >= 0 && action < PC_BIND_COUNT) ? s_menu_names[action] : "";
}

void pc_keybindings_key_label(int action, char* buf, size_t n) {
    PCInputCode code = g_pc_keybindings.key[action];

    if (code < 0) {
        snprintf(buf, n, "-");
    } else if (code & PC_INPUT_MOUSE_BIT) {
        snprintf(buf, n, "%s", input_code_name(code));
    } else {
        const char* name = SDL_GetScancodeName((SDL_Scancode)code);

        snprintf(buf, n, "%s", (name != NULL && name[0] != '\0') ? name : "?");
    }
}

static const char* pad_label(PCPadInput in) {
    if (in == PC_PAD_TRIGGER_LEFT) return "LT";
    if (in == PC_PAD_TRIGGER_RIGHT) return "RT";
    if (in >= 0 && in < SDL_CONTROLLER_BUTTON_MAX && s_pad_labels[in] != NULL) return s_pad_labels[in];
    return pad_input_name(in);
}

void pc_keybindings_pad_label(int action, char* buf, size_t n) {
    const PCPadInput* in = g_pc_keybindings.pad[action];

    if (in[0] == PC_PAD_NONE) {
        snprintf(buf, n, "-");
    } else if (in[1] == PC_PAD_NONE) {
        snprintf(buf, n, "%s", pad_label(in[0]));
    } else {
        snprintf(buf, n, "%s, %s", pad_label(in[0]), pad_label(in[1]));
    }
}

void pc_keybindings_set_key(int action, PCInputCode code) {
    PCInputCode old;

    if (action < 0 || action >= PC_BIND_COUNT) return;
    if (!s_ready) pc_keybindings_reset();
    old = g_pc_keybindings.key[action];
    for (int i = 0; i < PC_BIND_COUNT; i++) {
        if (i != action && g_pc_keybindings.key[i] == code) {
            g_pc_keybindings.key[i] = old;
        }
    }
    g_pc_keybindings.key[action] = code;
}

void pc_keybindings_set_pad(int action, PCPadInput input) {
    PCPadInput old;

    if (action < 0 || action >= PC_BIND_COUNT) return;
    if (!s_ready) pc_keybindings_reset();
    old = g_pc_keybindings.pad[action][0];
    if (old == input) {
        old = g_pc_keybindings.pad[action][1];
    }
    for (int i = 0; i < PC_BIND_COUNT; i++) {
        PCPadInput* in = g_pc_keybindings.pad[i];

        if (i == action) continue;
        for (int j = 0; j < PC_PAD_BINDS; j++) {
            if (in[j] == input) {
                in[j] = old;
            }
        }
        /* keep the bound ones first */
        if (in[0] == PC_PAD_NONE && in[1] != PC_PAD_NONE) {
            in[0] = in[1];
            in[1] = PC_PAD_NONE;
        }
        if (in[0] != PC_PAD_NONE && in[0] == in[1]) {
            in[1] = PC_PAD_NONE;
        }
    }
    g_pc_keybindings.pad[action][0] = input;
    g_pc_keybindings.pad[action][1] = PC_PAD_NONE;
}
