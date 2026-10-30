/* pc_typing.c - see pc_typing.h */
#include "pc_typing.h"

#include "ko_charset.h"
#include "ko_ime.h"
#include "pc_ko.h"
#include "pc_settings.h"

int g_pc_typing_mode = 0;
int g_pc_editor_active = 0;

static int s_queue[PC_TYPING_QUEUE_SIZE];
static int s_head = 0;
static int s_count = 0;

static KoIme s_ime;
static int s_preedit = -1;     /* built-in input: glyph being composed, or -1 */
static int s_has_preedit = 0;  /* composition operations went to the editor */

static void queue_clear(void) {
    s_head = 0;
    s_count = 0;
}

static void push(int op) {
    if (s_count < PC_TYPING_QUEUE_SIZE) {
        s_queue[(s_head + s_count) % PC_TYPING_QUEUE_SIZE] = op;
        s_count++;
    }
}

extern u32 pc_frame_counter;
static u32 s_polled_frame = (u32)-8;

int pc_typing_editor_listening(void) {
    return pc_frame_counter - s_polled_frame <= 2;
}

int pc_typing_pop(int* op) {
    s_polled_frame = pc_frame_counter; /* the editor asks every frame it takes input */
    if (s_count == 0) {
        return 0;
    }
    *op = s_queue[s_head];
    s_head = (s_head + 1) % PC_TYPING_QUEUE_SIZE;
    s_count--;
    return 1;
}

static void drop_composition(void) {
    KoImeMode mode = s_ime.mode;

    ko_ime_init(&s_ime);
    s_ime.mode = mode;
    s_preedit = -1;
    s_has_preedit = 0;
}

int pc_typing_system_ime(void) {
    return g_pc_settings.text_input == 1;
}

void pc_typing_preedit_rejected(void) {
    /* The system's input method keeps composing; what part of its text did
     * fit is still removed by the next PC_TYPING_CMD_PREEDIT_CLEAR */
    if (!pc_typing_system_ime()) {
        drop_composition();
    }
}

void pc_typing_commit_composition(void) {
    if (pc_typing_system_ime() && s_has_preedit) {
        /* Otherwise the input method would commit the same text again */
        SDL_ClearComposition();
    }
    drop_composition();
}

int pc_typing_korean(void) {
    return g_pc_ko_mode && !pc_typing_system_ime() && s_ime.mode == KO_IME_MODE_KO;
}

/* One-byte code of a typed ASCII character. Without the Korean patch only
 * the characters the Japanese font certainly has are allowed. */
static int ascii_code(uint16_t c) {
    if (c >= 0x80) {
        return -1;
    }
    if (g_pc_ko_mode) {
        return ko_charset_ascii((char)c);
    }
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        return c;
    }
    switch (c) {
        case ' ': case '!': case '?': case ',': case '.':
            return c;
        default:
            return -1;
    }
}

static void clear_preedit(void) {
    if (s_has_preedit) {
        push(PC_TYPING_CMD_PREEDIT_CLEAR);
        s_has_preedit = 0;
    }
    s_preedit = -1;
}

/* Queues one character; characters the font cannot show are dropped (a
 * syllable outside KS X 1001, or any Hangul without the Korean patch). */
static void push_unit(uint16_t unit, int preedit) {
    int glyph = g_pc_ko_mode ? ko_charset_glyph(unit) : -1;
    int op;

    if (glyph >= 0) {
        op = PC_TYPING_PAIR | glyph;
    } else if (ascii_code(unit) >= 0) {
        op = ascii_code(unit);
    } else {
        return;
    }
    push(preedit ? (op | PC_TYPING_PREEDIT) : op);
    if (preedit) {
        s_has_preedit = 1;
    }
}

/* Committed text from the built-in input method. Its first unit finishes
 * the syllable that is in the editor as the composition. */
static void emit_text(const uint16_t* units, int n) {
    int i;

    if (n > 0) {
        clear_preedit();
    }
    for (i = 0; i < n; i++) {
        push_unit(units[i], FALSE);
    }
}

static void update_preedit(void) {
    uint16_t p = ko_ime_preedit(&s_ime);
    int glyph = -1;

    if (p != 0) {
        glyph = ko_charset_glyph(p);
        if (glyph < 0 && p >= 0xAC00) {
            /* Not in KS X 1001: show its initial consonant while composing */
            glyph = KO_GLYPH_JAMO_FIRST + (p - 0xAC00) / (21 * 28);
        }
    }
    if (glyph != s_preedit) {
        clear_preedit();
        if (glyph >= 0) {
            push(PC_TYPING_PREEDIT | PC_TYPING_PAIR | glyph);
            s_has_preedit = 1;
            s_preedit = glyph;
        }
    }
}

static void flush(void) {
    uint16_t out[KO_IME_MAX_COMMIT];
    int n = ko_ime_flush(&s_ime, out);

    emit_text(out, n);
    update_preedit();
}

int pc_typing_toggle_korean(void) {
    if (!g_pc_typing_mode || pc_typing_system_ime()) {
        return 0;
    }
    return pc_typing_osk_toggle_korean();
}

int pc_typing_osk_toggle_korean(void) {
    uint16_t out[KO_IME_MAX_COMMIT];
    int n;

    if (!g_pc_ko_mode || !g_pc_editor_active) {
        return 0;
    }
    n = ko_ime_toggle_mode(&s_ime, out);
    emit_text(out, n);
    update_preedit();
    return 1;
}

int pc_typing_ime_korean(void) {
    return g_pc_ko_mode && s_ime.mode == KO_IME_MODE_KO;
}

/* A key through the built-in input method */
static void builtin_key(int key) {
    uint16_t out[KO_IME_MAX_COMMIT];
    int n;

    if (key == PC_TYPING_CMD_BACKSPACE && ko_ime_backspace(&s_ime)) {
        update_preedit();
    } else if (key >= PC_TYPING_CMD_BACKSPACE) {
        flush();
        push(key);
    } else if (g_pc_ko_mode) {
        n = ko_ime_key(&s_ime, (char)key, out);
        emit_text(out, n);
        update_preedit();
    } else if (ascii_code((uint16_t)key) >= 0) {
        push(ascii_code((uint16_t)key));
    }
}

int pc_typing_key(int key) {
    if (!g_pc_typing_mode || !g_pc_editor_active) {
        return 0;
    }
    if (pc_typing_system_ime() && key < PC_TYPING_CMD_BACKSPACE) {
        push_unit((uint16_t)key, FALSE);
    } else {
        builtin_key(key);
    }
    return 1;
}

int pc_typing_osk_key(int key) {
    if (!g_pc_editor_active) {
        return 0;
    }
    builtin_key(key);
    return 1;
}

void pc_typing_editor_open(int latin) {
    queue_clear();
    ko_ime_init(&s_ime);
    s_preedit = -1;
    s_has_preedit = 0;
    pc_ko_osk_reset();
    if (g_pc_ko_mode) {
        uint16_t unused[KO_IME_MAX_COMMIT];

        ko_ime_set_mode(&s_ime, latin ? KO_IME_MODE_EN : KO_IME_MODE_KO, unused);
        g_pc_typing_mode = 1;
    }
    if (pc_typing_system_ime()) {
        g_pc_typing_mode = 1;
        SDL_StartTextInput(); /* shows the on-screen keyboard on phones */
    } else if (g_pc_typing_mode) {
        SDL_StopTextInput(); /* keep the OS input method out of the way */
    }
    g_pc_editor_active = 1;
}

void pc_typing_editor_close(void) {
    g_pc_editor_active = 0;
    queue_clear();
    drop_composition();
    if (pc_typing_system_ime()) {
        SDL_StopTextInput();
    }
}

/* Queues each code point of UTF-8 text (BMP only) */
static void each_unit(const char* text, int preedit) {
    const u8* p = (const u8*)text;

    while (*p != '\0') {
        u32 cp;

        if (p[0] < 0x80) {
            cp = *p++;
        } else if ((p[0] & 0xE0) == 0xC0 && p[1] != '\0') {
            cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if ((p[0] & 0xF0) == 0xE0 && p[1] != '\0' && p[2] != '\0') {
            cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        } else {
            p++;
            continue;
        }
        push_unit((uint16_t)cp, preedit);
    }
}

void pc_typing_system_editing(const char* text) {
    if (!g_pc_typing_mode || !g_pc_editor_active || !pc_typing_system_ime()) {
        return;
    }
    clear_preedit();
    each_unit(text, TRUE);
}

void pc_typing_system_text(const char* text) {
    if (!g_pc_typing_mode || !g_pc_editor_active || !pc_typing_system_ime()) {
        return;
    }
    clear_preedit();
    each_unit(text, FALSE);
}

/* Character a key gives on a US QWERTY layout, or 0 */
static int key_char(SDL_Keycode sym, Uint16 mod) {
    static const char shifted_digits[] = ")!@#$%^&*(";
    int shift = (mod & KMOD_SHIFT) != 0;

    if (sym >= SDLK_a && sym <= SDLK_z) {
        /* Caps Lock must not turn ㄱ into ㄲ */
        int upper = shift ^ (!pc_typing_korean() && (mod & KMOD_CAPS) != 0);

        return upper ? (int)(sym - SDLK_a + 'A') : (int)sym;
    }
    if (sym >= SDLK_0 && sym <= SDLK_9) {
        return shift ? shifted_digits[sym - SDLK_0] : (int)sym;
    }
    if (sym >= SDLK_KP_1 && sym <= SDLK_KP_9) {
        return '1' + (int)(sym - SDLK_KP_1);
    }
    switch (sym) {
        case SDLK_KP_0: return '0';
        case SDLK_SPACE: return ' ';
        case SDLK_MINUS: return shift ? '_' : '-';
        case SDLK_EQUALS: return shift ? '+' : '=';
        case SDLK_LEFTBRACKET: return shift ? '{' : '[';
        case SDLK_RIGHTBRACKET: return shift ? '}' : ']';
        case SDLK_BACKSLASH: return shift ? '|' : '\\';
        case SDLK_SEMICOLON: return shift ? ':' : ';';
        case SDLK_QUOTE: return shift ? '"' : '\'';
        case SDLK_BACKQUOTE: return shift ? '~' : '`';
        case SDLK_COMMA: return shift ? '<' : ',';
        case SDLK_PERIOD: return shift ? '>' : '.';
        case SDLK_SLASH: return shift ? '?' : '/';
        default: return 0;
    }
}

void pc_typing_handle_event(const SDL_Event* event) {
    SDL_Keycode sym;
    Uint16 mod;
    int c;

    if (!g_pc_editor_active) {
        return;
    }
    if (event->type == SDL_TEXTEDITING) {
        pc_typing_system_editing(event->edit.text);
        return;
    }
    if (event->type == SDL_TEXTINPUT) {
        pc_typing_system_text(event->text.text);
        return;
    }
    if (event->type != SDL_KEYDOWN) {
        return;
    }
    sym = event->key.keysym.sym;
    mod = event->key.keysym.mod;

    if (sym == SDLK_TAB && !event->key.repeat) {
        g_pc_typing_mode ^= 1;
        queue_clear();
        drop_composition();
        if (g_pc_typing_mode && !pc_typing_system_ime()) {
            SDL_StopTextInput();
        }
        return;
    }
    if (!g_pc_typing_mode) {
        return;
    }

    if (event->key.keysym.scancode == SDL_SCANCODE_LANG1 || sym == SDLK_RALT ||
        (sym == SDLK_SPACE && (mod & KMOD_SHIFT))) {
        if (!event->key.repeat) {
            pc_typing_toggle_korean();
        }
        return;
    }
    switch (sym) {
        case SDLK_BACKSPACE: pc_typing_key(PC_TYPING_CMD_BACKSPACE); return;
        case SDLK_LEFT:      pc_typing_key(PC_TYPING_CMD_LEFT); return;
        case SDLK_RIGHT:     pc_typing_key(PC_TYPING_CMD_RIGHT); return;
        case SDLK_UP:        pc_typing_key(PC_TYPING_CMD_UP); return;
        case SDLK_DOWN:      pc_typing_key(PC_TYPING_CMD_DOWN); return;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (!event->key.repeat) {
                pc_typing_key((mod & (KMOD_CTRL | KMOD_SHIFT)) ? PC_TYPING_CMD_END : PC_TYPING_CMD_ENTER);
            }
            return;
        default:
            break;
    }
    if ((mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) || pc_typing_system_ime()) {
        return; /* with the system's input method, text comes as SDL_TEXTINPUT */
    }
    c = key_char(sym, mod);
    if (c != 0) {
        pc_typing_key(c);
    }
}
