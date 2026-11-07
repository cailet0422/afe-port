/* pc_touch_full.h - the full touch mode (settings.ini touch_controls = 2)
 *
 * No pad on the screen: the game is played with the fingers on the world.
 *
 * - A tap on the ground walks there; a finger held still runs to where it
 *   is held; a finger dragged runs the way it is dragged (from where it
 *   landed, like a stick nobody sees).
 * - A tap on a villager walks up to them and talks; a tap while a message
 *   is up goes on (A), a tap on a choice picks it.
 * - A tap on a thing walks up to it and uses it: items, flowers and weeds
 *   are picked up (B); trees are shaken (or chopped with the axe), signs,
 *   mailboxes and furniture are used (A); with the shovel a tap on the
 *   ground digs there (or fills the hole), with the net or the rod a tap
 *   near swings or casts that way. A tap on the player is A.
 * - Icons at the top (drawn by pc_gui_touch.cpp): the pause menu, a save,
 *   the gamepad, the pockets and the map on the right, the furniture mode
 *   in one's own house; between them the tools in the pockets, swiped or
 *   tapped left and right to take another in hand.
 * - The pockets and the map take taps and drags (pc_touch_full.c); other
 *   menus, the NES games and anything else that wants buttons bring the
 *   GameCube pad of pc_touch.c up by themselves, and the gamepad icon shows
 *   it whenever it is wanted.
 *
 * pc_touch.c hands the fingers here first when touch_controls is 2. */
#ifndef PC_TOUCH_FULL_H
#define PC_TOUCH_FULL_H

#include "pc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;

/* touch_controls = 2 */
int pc_touch_full_on(void);

/* Fingers in window fractions (0-1); down returns 1 if this mode took the
 * finger (else the GameCube pad of pc_touch.c gets it) */
int pc_touch_full_finger_down(s64 id, float x, float y);
void pc_touch_full_finger_move(s64 id, float x, float y);
/* 1 if the finger was this mode's */
int pc_touch_full_finger_up(s64 id);

/* Adds the sticks and buttons this mode presses for the player */
void pc_touch_full_read(u16* buttons, s8* stick_x, s8* stick_y, s8* cstick_x, s8* cstick_y);

/* After game_main (pc_pause_menu_draw): what is changed in the game itself
 * (the tool in hand, the cursor of the pockets, the room's furniture) */
void pc_touch_full_game_frame(struct game_s* game);

/* 1 while the GameCube pad of pc_touch.c is up (asked with the gamepad icon,
 * or a menu or game only buttons work in) */
int pc_touch_full_pad_shown(void);

/* --- The icons, for pc_gui_touch.cpp ------------------------------------ */

enum {
    PC_HUD_MENU,     /* the pause menu */
    PC_HUD_SAVE,     /* saves the game (pc_autosave_request_save) */
    PC_HUD_PAD,      /* the GameCube pad on or off */
    PC_HUD_BAG,      /* the pockets */
    PC_HUD_MAP,      /* the town map */
    PC_HUD_FURNITURE, /* the furniture mode (in one's own house) */
    PC_HUD_CLOSE,    /* closes the pockets or the map */
    PC_HUD_TOOL,     /* the tool in hand (the middle of the strip) */
    PC_HUD_TOOL_PREV,
    PC_HUD_TOOL_NEXT,
    /* the furniture mode's own */
    PC_HUD_FURN_DONE,
    PC_HUD_FURN_ROT_L,
    PC_HUD_FURN_ROT_R,
    PC_HUD_FURN_STORE,
    PC_HUD_FURN_SLOT, /* a piece in the pockets (item = its item, index = the slot) */
    PC_HUD_CAM_ANGLE, /* in a room: the camera's angle (C left/right) */
    PC_HUD_CAM_ZOOM,  /* in a room: the camera's distance (C up/down) */
    PC_HUD_LIGHT,     /* in one's own house: the light switch (the D-pad) */
    PC_HUD_NES_EXIT,  /* an NES game: leaves it (L+R+Z) */
    PC_HUD_DONE,      /* a menu: finishes (the text editor's START, ...) */
    PC_HUD_KEY,       /* a menu's key (label; index = the key, pc_touch_menus.c) */
    PC_HUD_KIND_COUNT
};

/* What a tool icon shows */
enum {
    PC_GLYPH_NONE, /* an empty hand */
    PC_GLYPH_NET,
    PC_GLYPH_AXE,
    PC_GLYPH_SHOVEL,
    PC_GLYPH_ROD,
    PC_GLYPH_UMBRELLA,
    PC_GLYPH_BALLOON,
    PC_GLYPH_PINWHEEL,
    PC_GLYPH_FAN,
    PC_GLYPH_FLOWER,
    PC_GLYPH_POPPER,
    PC_GLYPH_OTHER,
    PC_GLYPH_FURNITURE,
};

/* What a menu's key shows (PC_HUD_KEY) */
enum {
    PC_KEYG_TEXT,  /* its label */
    PC_KEYG_UP,
    PC_KEYG_DOWN,
    PC_KEYG_LEFT,
    PC_KEYG_RIGHT,
    PC_KEYG_PLAY,
    PC_KEYG_BACK,  /* backspace */
};

typedef struct {
    int kind;      /* PC_HUD_ */
    float x, y;    /* centre, fractions of the window's width and height */
    float r;       /* radius, fraction of the window's shorter side */
    int glyph;     /* PC_GLYPH_ (tools, the furniture slots), PC_KEYG_ (PC_HUD_KEY) */
    int gold;      /* a golden tool */
    int pressed;   /* under a finger */
    int active;    /* on (the gamepad shown, the furniture mode) or picked */
    int index;     /* the pocket slot (PC_HUD_FURN_SLOT), the key (PC_HUD_KEY) */
    u16 item;
    const char* label; /* PC_HUD_KEY: its text (UTF-8) */
} PCTouchHudIcon;

/* The icons to draw now; returns how many (0: none) */
int pc_touch_full_hud(PCTouchHudIcon* out, int max);

/* The furniture mode, for the drawing: 1 while it is on. The outline of
 * the picked piece (four corners, window fractions), 0 if none; ok = it can
 * stay where it is. */
int pc_touch_full_furniture_mode(void);
int pc_touch_full_furniture_outline(float corners[8], int* ok);
/* The room's floor as four corners (window fractions), for a grid */
int pc_touch_full_furniture_floor(float corners[8], int* units_x, int* units_z);
/* A line of help for the furniture mode (UTF-8; English or Korean) */
const char* pc_touch_full_furniture_hint(int korean);

#ifdef __cplusplus
}
#endif

#endif /* PC_TOUCH_FULL_H */
