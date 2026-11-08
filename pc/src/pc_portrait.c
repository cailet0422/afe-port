/* pc_portrait.c - the full touch mode held upright (a window taller than wide)
 *
 * The 3D fills the window (vert+) and is drawn closer, from a camera a little
 * lower and nearer than the game's (pc_portrait_camera, from m_camera2.c);
 * the game's 2D keeps its shape in the window's width, placed by part
 * (pc_gx.c, PC_NOOP_UI_PLACE markers from m_msg.c and m_play.c): the message
 * window at the top under the icons, larger; the clock at the bottom; the
 * menus in the middle. With the Korean patch the choices are big buttons at
 * the bottom (drawn by pc_gui_touch.cpp, the game's own window not drawn);
 * on the original disc, whose kana this port does not write out, the game's
 * window goes to the bottom, larger.
 *
 * On Android the orientation follows the setting (pc_portrait_apply). */
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_ko.h"
#include "pc_touch_full.h"
#include "ko_charset.h"

#include "m_common_data.h"
#include "m_msg.h"
#include "m_choice.h"
#include "charmap.h"

#include <math.h>
#include <string.h>

#ifdef __ANDROID__
#include <SDL.h>
#include <jni.h>
#endif

/* --- The camera ----------------------------------------------------------------- */

void pc_portrait_camera(float* dist, short* pitch, int indoor) {
    /* the normal camera looks down at 45 degrees (direction x -135) from 620:
     * held upright, from lower and nearer, as a phone's game would */
    if (indoor) {
        *dist *= 0.92f;
        *pitch = (short)(*pitch - 0x0400); /* (5.6 degrees lower: the walls) */
    } else {
        *dist *= 0.86f;
        *pitch = (short)(*pitch - 0x0800); /* 11 degrees lower: 34 above the ground (lower, the
                                              trees in front hide the player) */
    }
}

/* --- The choices as buttons ---------------------------------------------------------- */

int pc_portrait_choice_buttons(void) {
    return g_pc_ko_mode && pc_portrait_layout();
}

/* The buttons, from the bottom up: fractions of the window */
void pc_portrait_button_rect(int i, int n, float* x0, float* y0, float* x1, float* y1) {
    float aspect = g_pc_window_h > 0 ? (float)g_pc_window_w / (float)g_pc_window_h : 0.5f;
    float bh = 0.115f * aspect, gap = 0.035f * aspect; /* (by the width) */
    int from_bottom = n - 1 - i;

    *x0 = 0.08f;
    *x1 = 0.92f;
    *y1 = 0.93f - from_bottom * (bh + gap);
    *y0 = *y1 - bh;
}

int pc_portrait_button_at(float fx, float fy, int n) {
    float slack = 0.004f;
    int i;

    for (i = 0; i < n; i++) {
        float x0, y0, x1, y1;

        pc_portrait_button_rect(i, n, &x0, &y0, &x1, &y1);
        if (fx >= x0 && fx < x1 && fy >= y0 - slack && fy < y1 + slack) {
            return i;
        }
    }
    return -1;
}

static void utf8_put(char** o, char* end, unsigned u) {
    char* p = *o;

    if (u < 0x80 && p + 1 < end) {
        *p++ = (char)u;
    } else if (u < 0x800 && p + 2 < end) {
        *p++ = (char)(0xC0 | (u >> 6));
        *p++ = (char)(0x80 | (u & 63));
    } else if (u >= 0x800 && p + 3 < end) {
        *p++ = (char)(0xE0 | (u >> 12));
        *p++ = (char)(0x80 | ((u >> 6) & 63));
        *p++ = (char)(0x80 | (u & 63));
    }
    *o = p;
}

/* The game's characters (Korean patch) as text: Hangul, letters, digits and
 * the signs the choices use */
static void game_text(const u8* s, int n, char* out, int cap) {
    const u8* str;
    char* o = out;
    char* end = out + cap;
    int i;

    n = pc_ko_expand(s, n, 5, &str); /* (dictionary codes: long names) */
    for (i = 0; i < n;) {
        int g = i + 1 < n ? ko_charset_decode(str + i) : -1;
        u8 c = str[i];
        unsigned u = 0;

        if (g >= 0) {
            utf8_put(&o, end, ko_charset_unicode(g));
            i += 2;
            continue;
        }
        if ((c >= CHAR_ZERO && c <= CHAR_NINE) || (c >= CHAR_A && c <= CHAR_Z) || (c >= CHAR_a && c <= CHAR_z) ||
            c == CHAR_SPACE || c == CHAR_EXCLAMATION || c == CHAR_QUESTIONMARK || c == CHAR_COMMA || c == CHAR_PERIOD ||
            c == CHAR_DASH || c == CHAR_COLON || c == CHAR_PERCENT || c == CHAR_AMPERSAND || c == CHAR_APOSTROPHE ||
            c == CHAR_OPEN_PARENTHESIS || c == CHAR_CLOSE_PARENTHESIS || c == CHAR_QUOTATION) {
            u = c;
        } else if (c == CHAR_TILDE) {
            u = '~';
        } else if (c == CHAR_SYMBOL_HEART) {
            u = 0x2665;
        } else if (c == CHAR_SYMBOL_MUSIC_NOTE) {
            u = 0x266A;
        }
        if (u != 0) {
            utf8_put(&o, end, u);
        }
        i++;
    }
    while (o > out && o[-1] == ' ') {
        o--;
    }
    *o = '\0';
}

int pc_portrait_choices(char text[][PC_PORTRAIT_CHOICE_LEN], int max, int* cursor_row) {
    mChoice_c* choice = mChoice_Get_base_window_p();
    int n, i;

    if (!pc_portrait_choice_buttons() || choice == NULL || !choice->window_visible_flag ||
        choice->main_index == mChoice_MAIN_HIDE || choice->main_index == mChoice_MAIN_DISAPPEAR) {
        return 0;
    }
    n = choice->data.choice_num;
    if (n > max) {
        n = max;
    }
    for (i = 0; i < n; i++) {
        game_text(choice->data.strings[i], choice->data.string_lens[i], text[i], PC_PORTRAIT_CHOICE_LEN);
    }
    *cursor_row = choice->selected_choice_idx; /* (the cursor: a tap sets it, pc_touch_full.c) */
    return n;
}

/* --- Where the message window and the choices are ----------------------------------- */

void pc_gx_ui_place_center(int place, float* cx, float* cy, float* h) {
    if (place == PC_UI_PLACE_MSG) {
        mMsg_Window_c* msg = mMsg_Get_base_window_p();

        *cx = msg != NULL ? msg->center_x : 160.0f;
        *cy = msg != NULL ? msg->center_y : 190.0f;
        *h = 92.0f; /* (the window and its name above, at its full scale) */
    } else {
        mChoice_c* choice = mChoice_Get_base_window_p();
        int n = choice != NULL ? choice->data.choice_num : 2;

        *cx = choice != NULL ? choice->center_x : 230.0f;
        *cy = choice != NULL ? choice->center_y : 150.0f;
        *h = 16.0f * (float)(n > 0 ? n : 2) + 16.0f;
    }
}

/* --- Android's orientation ------------------------------------------------------------ */

void pc_portrait_apply(void) {
#ifdef __ANDROID__
    /* ActivityInfo: SCREEN_ORIENTATION_SENSOR_LANDSCAPE 6, SENSOR_PORTRAIT 7,
     * FULL_USER 13 (turns with the phone, unless the user locked it) */
    static int s_last = -1;
    int want = 6;
    JNIEnv* env;
    jobject activity;
    jclass cls;
    jmethodID mid;

    if (g_pc_settings.touch_controls == 2) {
        want = g_pc_settings.touch_orientation == 1 ? 6 : g_pc_settings.touch_orientation == 2 ? 7 : 13;
    }
    if (want == s_last) {
        return;
    }
    env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    activity = (jobject)SDL_AndroidGetActivity();
    if (env == NULL || activity == NULL) {
        return;
    }
    cls = (*env)->GetObjectClass(env, activity);
    mid = (*env)->GetMethodID(env, cls, "setRequestedOrientation", "(I)V");
    if (mid != NULL) {
        (*env)->CallVoidMethod(env, activity, mid, (jint)want);
        s_last = want;
        printf("[PC] orientation %d\n", want);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
#endif
}
