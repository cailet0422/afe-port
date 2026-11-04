/* pc_autosave.c - the port saves by itself (see pc_autosave.h) */
#include "pc_autosave.h"
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_mp_game.h"
#include "pc_rom_dir.h"
#include "pc_pause_menu.h"
#include "pc_gui.h"

#include "game.h"
#include "m_common_data.h"
#include "m_field_info.h"
#include "m_msg.h"
#include "m_play.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_private.h"
#include "m_scene_table.h"
#include "m_event.h"
#include "m_flashrom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int mCD_PC_AutoSaveSnapshot(Save* out, GAME_PLAY* play);
extern int mCD_PC_FirstSave(void);
extern void aMR_PC_StorageIntoCopy(const void* floor, void* copy, int size);
extern int mCD_PC_PatchTraveller(Save* file_save);
extern int mCD_PC_LandFileInfo(const char** name, int* file_size, int* save_ofs0, int* save_ofs1);
extern const char* pc_card_dir(s32 chan);
extern s32 CARDDelete(s32 chan, const char* fileName);

#define SCENE_GAP_MS 20000 /* at most one save per 20 s from scene changes */

static Save* s_buf = NULL; /* (0x2E000 bytes: not on the stack) */
static Save* s_file = NULL;
static u32 s_last_ms = 0;
static u32 s_quiet_since = 0; /* the player standing or walking since (an item in the air, a hole opening: done) */
#define QUIET_MS 3000
static GAME_PLAY* s_play = NULL;
static int s_first_noted = 0; /* (the notice of a new town's first save, once) */
static int s_stopped = 0;     /* a town file deleted: no saving until the game restarts */

int pc_resetti_enabled(void) {
    return !g_pc_settings.disable_resetti;
}

void pc_autosave_unit_written(unsigned short old_item, unsigned short new_item) {
    if (new_item == RSV_NO || old_item == RSV_NO) {
        s_quiet_since = SDL_GetTicks(); /* the game busy with a unit: a while longer */
    }
}

static int buffers(void) {
    if (s_buf == NULL) s_buf = (Save*)malloc(sizeof(Save));
    if (s_file == NULL) s_file = (Save*)malloc(sizeof(Save));
    return s_buf != NULL && s_file != NULL;
}

/* the save part of a town file: read (copy 0) or write (both copies) */
static int file_part(int chan, int write, const Save* data, Save* out) {
    const char* name;
    int size, o0, o1;
    char path[600];
    FILE* f;
    int ok;

    mCD_PC_LandFileInfo(&name, &size, &o0, &o1);
    snprintf(path, sizeof(path), "%s/%s", pc_card_dir(chan), name);
    f = pc_fopen_utf8(path, write ? "r+b" : "rb");
    if (f == NULL) return 0;
    if (write) {
        /* the main copy, then the backup: a cut write leaves one good copy */
        ok = fseek(f, o0, SEEK_SET) == 0 && fwrite(data, sizeof(Save), 1, f) == 1 && fflush(f) == 0 &&
             fseek(f, o1, SEEK_SET) == 0 && fwrite(data, sizeof(Save), 1, f) == 1;
    } else {
        ok = fseek(f, o0, SEEK_SET) == 0 && fread(out, sizeof(Save), 1, f) == 1;
    }
    if (fclose(f) != 0) ok = 0;
    return ok;
}

/* the card (0 = A, 1 = B) with the town of this land id and name */
static int town_card(u16 land_id, const u8* land_name) {
    for (int chan = 0; chan < 2; chan++) {
        if (file_part(chan, 0, NULL, s_file) && s_file->save.land_info.id == land_id &&
            memcmp(s_file->save.land_info.name, land_name, LAND_NAME_SIZE) == 0) {
            return chan;
        }
    }
    return -1;
}

/* save_now's results */
#define SAVE_FAILED 0
#define SAVE_DONE 1
#define SAVE_NOT_ALLOWED (-1) /* the game would not save now (the gyroid neither) */

/* force: a save asked for by hand (pc_autosave_request_save), also with the
 * autosave off; room: the player is in a house's room, whose storage
 * furniture holds its items out of the Save while the room is loaded */
static int save_now(GAME_PLAY* play, const char* why, int force, int room) {
    Private_c* priv = Common_Get(now_private);
    int chan;

    if ((!g_pc_settings.autosave && !force) || s_stopped || (g_pc_deterministic && !g_pc_autosave_test) ||
        priv == NULL || !buffers()) {
        return SAVE_FAILED;
    }
    if (Common_Get(player_no) == mPr_FOREIGNER) {
        /* visiting another town (the multiplayer's trip): our own town as it
         * was left at the station, with our pockets and bells of now */
        chan = town_card(priv->player_ID.land_id, priv->player_ID.land_name);
        if (chan < 0 || !mCD_PC_PatchTraveller(s_file) || !file_part(chan, 1, s_file, NULL)) return SAVE_FAILED;
        printf("[AUTOSAVE] %s: the traveller's own town (card %c)\n", why, 'A' + chan);
    } else {
        if (!mCD_PC_AutoSaveSnapshot(s_buf, play)) {
            /* a new town has no file until its first save (the gyroid's):
             * make it the game's way, then write the town as it is now */
            if (!mCD_PC_FirstSave()) {
                if (g_pc_verbose) printf("[AUTOSAVE] %s: not now (the gyroid would not save either)\n", why);
                return SAVE_NOT_ALLOWED;
            }
            printf("[AUTOSAVE] %s: the town's first save (a new town file)\n", why);
            if (!s_first_noted) {
                s_first_noted = 1;
                pc_gui_notify(pc_gui_korean() ? "자동 저장: 마을을 처음 저장했습니다. 이제 자동으로 저장됩니다."
                                              : "Autosave: the town is saved for the first time; it now saves by itself.");
            }
            if (!mCD_PC_AutoSaveSnapshot(s_buf, play)) {
                s_last_ms = SDL_GetTicks();
                return SAVE_DONE; /* (the first save is this one) */
            }
        }
        if (room) {
            /* the items in the room's storage furniture, where leaving the
             * room puts them back (afe-decomp ac_my_room.c) */
            aMR_PC_StorageIntoCopy(&Common_Get(save).save, &s_buf->save, (int)sizeof(Save_t));
            s_buf->save.save_check.checksum = 0;
            s_buf->save.save_check.checksum = mFRm_GetFlatCheckSum((u16*)s_buf, sizeof(Save), 0);
        }
        chan = town_card(s_buf->save.land_info.id, s_buf->save.land_info.name);
        if (chan < 0 || s_file->save.copy_protect != s_buf->save.copy_protect) {
            if (g_pc_verbose) printf("[AUTOSAVE] %s: no file of this town (card %d)\n", why, chan);
            return SAVE_FAILED; /* not this save's file */
        }
        if (!file_part(chan, 1, s_buf, NULL)) return SAVE_FAILED;
        printf("[AUTOSAVE] %s (card %c)\n", why, 'A' + chan);
    }
    s_last_ms = SDL_GetTicks();
    return SAVE_DONE;
}

/* where the town (or a shop, a villager's house) is the Save itself, and
 * nothing in hand or on the way: outdoors in a town, or a building that is
 * not a player's room (whose storage furniture holds items out of the Save).
 * A save by hand may also be in a room (save_now puts the items back into
 * its copy) and needs no wait. */
static int s_why = 0; /* (the last reason for waiting, for the log and the save by hand) */

static int quiet_place(GAME_PLAY* play, int outdoors_only, int by_hand) {
    PLAYER_ACTOR* pl;
    int type, idx;

    s_why = 1;
    if (play == NULL || play->fb_fade_type != FADE_TYPE_NONE || g_pc_paused) return 0;
    s_why = 2;
    if (!mMsg_CHECK_MAINHIDE() || play->submenu.process_status != mSM_PROCESS_WAIT) return 0;
    s_why = 3;
    type = mFI_GET_TYPE(mFI_GetFieldId());
    if ((type == mFI_FIELD_PLAYER0_ROOM && !by_hand) || mFI_CheckInIsland()) return 0;
    if (outdoors_only && (Save_Get(scene_no) != SCENE_FG || type != mFI_FIELD_FG)) return 0;
    s_why = 4;
    pl = GET_PLAYER_ACTOR(play);
    if (pl == NULL) return 0;
    idx = pl->now_main_index;
    if (idx != mPlayer_INDEX_WAIT && idx != mPlayer_INDEX_WALK && idx != mPlayer_INDEX_RUN &&
        idx != mPlayer_INDEX_DASH) {
        s_quiet_since = SDL_GetTicks();
        return 0;
    }
    s_why = 5;
    return by_hand || SDL_GetTicks() - s_quiet_since >= QUIET_MS;
}

/* The save by hand (the touch pad's SAVE, Ctrl+S): now if it can be, or a
 * notice saying why not */
static int s_by_hand = 0;

void pc_autosave_request_save(void) {
    s_by_hand = 1;
}

static void save_by_hand(GAME_PLAY* play) {
    int ko = pc_gui_korean();
    const char* note;
    int r;

    if (!quiet_place(play, 0, 1)) {
        switch (s_why) {
            case 2: note = ko ? "대화나 메뉴를 닫은 뒤에 저장하세요." : "Close the conversation or menu first, then save."; break;
            case 3: note = ko ? "섬에서는 저장할 수 없습니다." : "You cannot save on the island."; break;
            case 4: note = ko ? "하던 동작을 끝낸 뒤에 저장하세요." : "Finish what you are doing first, then save."; break;
            default: note = ko ? "지금은 저장할 수 없습니다. 게임 화면에서 다시 해 주세요." : "You cannot save right now."; break;
        }
        pc_gui_notify(note);
        printf("[AUTOSAVE] by hand: not now (%d)\n", s_why);
        return;
    }
    r = save_now(play, "by hand", 1, mFI_GET_TYPE(mFI_GetFieldId()) == mFI_FIELD_PLAYER0_ROOM);
    if (r == SAVE_DONE) {
        note = ko ? "저장했습니다." : "Saved.";
    } else if (r == SAVE_NOT_ALLOWED) {
        note = ko ? "지금은 게임이 저장을 허용하지 않습니다. 첫날은 주민과 인사한 뒤부터 저장됩니다."
                  : "The game does not allow saving now. On the first day, saving starts once you have met a villager.";
    } else {
        note = ko ? "저장하지 못했습니다." : "Could not save.";
    }
    pc_gui_notify(note);
}

void pc_autosave_scene_end(void) {
    int left = Common_Get(last_scene_no);

    s_play = NULL;
    if (mEv_CheckTitleDemo() != 0 || left == SCENE_PLAYERSELECT || left == SCENE_PLAYERSELECT_2 ||
        left == SCENE_PLAYERSELECT_3 || left == SCENE_PLAYERSELECT_SAVE || left == SCENE_START_DEMO ||
        left == SCENE_START_DEMO2 || left == SCENE_START_DEMO3 || left == SCENE_TITLE_DEMO) {
        return;
    }
    if (s_last_ms != 0 && SDL_GetTicks() - s_last_ms < SCENE_GAP_MS) return;
    save_now(NULL, "between scenes", 0, 0);
}

void pc_autosave_frame(struct game_s* game) {
    GAME_PLAY* play = (game != NULL && game->exec == play_main) ? (GAME_PLAY*)game : NULL;
    u32 every = (u32)g_pc_settings.autosave_minutes * 60000u;

    s_play = play;
    if (s_by_hand) {
        s_by_hand = 0;
        save_by_hand(play);
    }
    if (play == NULL || !g_pc_settings.autosave) return;
    if (s_last_ms == 0) s_last_ms = SDL_GetTicks(); /* (from the start of play) */
    if (SDL_GetTicks() - s_last_ms < every) return;
    if (!quiet_place(play, 1, 0)) {
        if (g_pc_verbose && SDL_GetTicks() % 5000 < 20) printf("[AUTOSAVE] waiting for a quiet moment outside (%d)\n", s_why);
        return;
    }
    if (save_now(play, "on the timer", 0, 0) != SAVE_DONE) s_last_ms = SDL_GetTicks() - every + 30000; /* again in 30 s */
}

void pc_autosave_quit(void) {
    /* the menu's Quit, the window closed: as things are now, if they can be */
    if (s_play != NULL && quiet_place(s_play, 0, 0)) {
        save_now(s_play, "on quitting", 0, 0);
    }
}

int pc_autosave_town_info(int chan, char* town, int town_size, char* players, int players_size) {
    int len = 0;

    if (town_size > 0) town[0] = '\0';
    if (players_size > 0) players[0] = '\0';
    if (chan < 0 || chan > 1 || !buffers() || !file_part(chan, 0, NULL, s_file)) return 0;
    pc_mp_game_name_utf8(s_file->save.land_info.name, LAND_NAME_SIZE, town, (size_t)town_size);
    for (int i = 0; i < PLAYER_NUM; i++) {
        Private_c* p = &s_file->save.private_data[i];
        char name[48];

        if (mPr_NullCheckPersonalID(&p->player_ID)) continue;
        pc_mp_game_name_utf8(p->player_ID.player_name, PLAYER_NAME_LEN, name, sizeof(name));
        if (name[0] == '\0') continue;
        len += snprintf(players + len, len < players_size ? (size_t)(players_size - len) : 0, "%s%s",
                        len > 0 ? ", " : "", name);
        if (len >= players_size) break;
    }
    return 1;
}

int pc_autosave_delete_town(int chan) {
    const char* name;
    int size, o0, o1;
    char dummy[40];

    if (chan < 0 || chan > 1) return 0;
    s_stopped = 1;
    mCD_PC_LandFileInfo(&name, &size, &o0, &o1);
    snprintf(dummy, sizeof(dummy), "%s_d", name); /* (a first save cut short leaves this) */
    CARDDelete(chan, dummy);
    if (CARDDelete(chan, name) != 0) {
        printf("[AUTOSAVE] could not delete the town file on card %c\n", 'A' + chan);
        return 0;
    }
    printf("[AUTOSAVE] deleted the town file on card %c\n", 'A' + chan);
    return 1;
}
