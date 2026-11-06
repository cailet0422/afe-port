/* pc_touch_room.c - the furniture mode of the full touch mode (pc_touch_full.h) */
#include "pc_touch_internal.h"

int pc_touch_room_available(GAME_PLAY* play) {
    (void)play;
    return 0;
}

int pc_touch_room_on(void) {
    return 0;
}

void pc_touch_room_toggle(void) {
}

int pc_touch_room_hud(PCTouchHudIcon* out, int n, int max) {
    (void)out;
    (void)max;
    return n;
}

void pc_touch_room_finger(int kind, s64 id, float x, float y) {
    (void)kind;
    (void)id;
    (void)x;
    (void)y;
}

void pc_touch_room_icon(int icon, int index) {
    (void)icon;
    (void)index;
}

void pc_touch_room_frame(GAME_PLAY* play) {
    (void)play;
}

int pc_touch_full_furniture_mode(void) {
    return 0;
}

int pc_touch_full_furniture_outline(float corners[8], int* ok) {
    (void)corners;
    (void)ok;
    return 0;
}

int pc_touch_full_furniture_floor(float corners[8], int* units_x, int* units_z) {
    (void)corners;
    (void)units_x;
    (void)units_z;
    return 0;
}

const char* pc_touch_full_furniture_hint(int korean) {
    (void)korean;
    return "";
}
