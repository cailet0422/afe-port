/* pc_typing.c - see pc_typing.h */
#include "pc_typing.h"

#include "ko_charset.h"
#include "ko_ime.h"
#include "pc_ko.h"

int g_pc_typing_mode = 0;
int g_pc_editor_active = 0;

static int s_queue[PC_TYPING_QUEUE_SIZE];
static int s_head = 0;
static int s_count = 0;

static KoIme s_ime;
static int s_preedit = -1; /* glyph of the composing syllable in the editor, or -1 */

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

int pc_typing_pop(int* op) {
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
}

void pc_typing_preedit_rejected(void) {
    drop_composition();
}

void pc_typing_commit_composition(void) {
    drop_composition();
}

int pc_typing_korean(void) {
    return g_pc_ko_mode && s_ime.mode == KO_IME_MODE_KO;
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

static void emit_preedit(int glyph) {
    if (glyph != s_preedit) {
        push(glyph >= 0 ? PC_TYPING_PREEDIT | glyph : PC_TYPING_PREEDIT_NONE);
        s_preedit = glyph;
    }
}

/* Committed text from the input method. The first unit finishes the
 * syllable that is already in the editor as the composition. */
static void emit_text(const uint16_t* units, int n) {
    int i;

    for (i = 0; i < n; i++) {
        int glyph = ko_charset_glyph(units[i]);

        if (i == 0 && s_preedit >= 0) {
            /* A syllable outside KS X 1001 has no glyph and is dropped */
            emit_preedit(glyph);
            push(PC_TYPING_CMD_COMMIT);
            s_preedit = -1;
        } else if (glyph >= 0) {
            push(PC_TYPING_PAIR | glyph);
        } else if (ascii_code(units[i]) >= 0) {
            push(ascii_code(units[i]));
        }
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
    emit_preedit(glyph);
}

static void flush(void) {
    uint16_t out[KO_IME_MAX_COMMIT];
    int n = ko_ime_flush(&s_ime, out);

    emit_text(out, n);
    update_preedit();
}

int pc_typing_toggle_korean(void) {
    uint16_t out[KO_IME_MAX_COMMIT];
    int n;

    if (!g_pc_ko_mode || !g_pc_typing_mode || !g_pc_editor_active) {
        return 0;
    }
    n = ko_ime_toggle_mode(&s_ime, out);
    emit_text(out, n);
    update_preedit();
    return 1;
}

int pc_typing_key(int key) {
    uint16_t out[KO_IME_MAX_COMMIT];
    int n;

    if (!g_pc_typing_mode || !g_pc_editor_active) {
        return 0;
    }
    if (key == PC_TYPING_CMD_BACKSPACE && ko_ime_backspace(&s_ime)) {
        update_preedit();
        return 1;
    }
    if (key >= PC_TYPING_CMD_BACKSPACE) {
        flush();
        push(key);
        return 1;
    }
    if (g_pc_ko_mode) {
        n = ko_ime_key(&s_ime, (char)key, out);
        emit_text(out, n);
        update_preedit();
    } else if (ascii_code((uint16_t)key) >= 0) {
        push(ascii_code((uint16_t)key));
    }
    return 1;
}

void pc_typing_editor_open(int latin) {
    queue_clear();
    ko_ime_init(&s_ime);
    s_preedit = -1;
    if (g_pc_ko_mode) {
        uint16_t unused[KO_IME_MAX_COMMIT];

        ko_ime_set_mode(&s_ime, latin ? KO_IME_MODE_EN : KO_IME_MODE_KO, unused);
        g_pc_typing_mode = 1;
    }
    if (g_pc_typing_mode) {
        SDL_StopTextInput(); /* keep the OS input method out of the way */
    }
    g_pc_editor_active = 1;
}

void pc_typing_editor_close(void) {
    g_pc_editor_active = 0;
    queue_clear();
    drop_composition();
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

    if (event->type != SDL_KEYDOWN || !g_pc_editor_active) {
        return;
    }
    sym = event->key.keysym.sym;
    mod = event->key.keysym.mod;

    if (sym == SDLK_TAB && !event->key.repeat) {
        g_pc_typing_mode ^= 1;
        queue_clear();
        drop_composition();
        if (g_pc_typing_mode) {
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
    if (mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) {
        return;
    }
    c = key_char(sym, mod);
    if (c != 0) {
        pc_typing_key(c);
    }
}
