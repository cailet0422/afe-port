/* pc_boot_fixups.c - byte-swap big-endian ROM data that the game reads as typed data.
 *
 * Most extracted assets are either raw bytes (textures, consumed by the
 * big-endian-aware texture decoder) or converted to native C initializers
 * (vertices, palettes). Blobs that are still raw bytes but are read as
 * u16/u32 values or structs are listed in the generated pc_data_fixups.c and
 * swapped once here. Called from boot.c before the game starts.
 */
#include "pc_platform.h"
#include "pc_bswap.h"
#include "pc_data_fixups.h"

#include "m_field_make.h"
#include "m_map_ovl.h"
#include "m_npc.h"
#include "dolphin/os.h"

static void swap16_at(u8* p) {
    u16 v;

    memcpy(&v, p, sizeof(v));
    v = pc_bswap16(v);
    memcpy(p, &v, sizeof(v));
}

static void swap32_at(u8* p) {
    u32 v;

    memcpy(&v, p, sizeof(v));
    v = pc_bswap32(v);
    memcpy(p, &v, sizeof(v));
}

/* Applies `swap` to each field offset of `count` records of `record_size` bytes. */
static void swap_records(u8* data, u32 size, u32 record_size, const u8* offsets16, int n16,
                         const u8* offsets32, int n32) {
    u32 rec;
    int i;

    for (rec = 0; rec + record_size <= size; rec += record_size) {
        for (i = 0; i < n16; i++) {
            swap16_at(data + rec + offsets16[i]);
        }
        for (i = 0; i < n32; i++) {
            swap32_at(data + rec + offsets32[i]);
        }
    }
}


static void apply_fixup(const PCDataFixup* fix) {
    u32 i;

    switch (fix->kind) {
        case PC_FIX_U16:
            for (i = 0; i + 2 <= fix->size; i += 2) {
                swap16_at(fix->data + i);
            }
            break;

        case PC_FIX_U32:
            for (i = 0; i + 4 <= fix->size; i += 4) {
                swap32_at(fix->data + i);
            }
            break;

        case PC_FIX_MOVE_ACTOR: {
            static const u8 o16[] = { offsetof(mFM_move_actor_data_c, name_id), offsetof(mFM_move_actor_data_c, arg) };
            static const u8 o32[] = { offsetof(mFM_move_actor_data_c, block_x), offsetof(mFM_move_actor_data_c, block_z),
                                      offsetof(mFM_move_actor_data_c, desired_block_kind) };
            swap_records(fix->data, fix->size, sizeof(mFM_move_actor_data_c), o16, 2, o32, 3);
            break;
        }

        case PC_FIX_COMBO_INFO: {
            static const u8 o16[] = { offsetof(mFM_combo_info_c, bg_id), offsetof(mFM_combo_info_c, fg_id) };
            swap_records(fix->data, fix->size, sizeof(mFM_combo_info_c), o16, 2, NULL, 0);
            break;
        }

        case PC_FIX_NPC_HOUSE: {
            static const u8 o16[] = { offsetof(mNpc_NpcHouseData_c, main_layer_id),
                                      offsetof(mNpc_NpcHouseData_c, secondary_layer_id) };
            swap_records(fix->data, fix->size, sizeof(mNpc_NpcHouseData_c), o16, 2, NULL, 0);
            break;
        }

        case PC_FIX_NPC_DEFAULT: {
            static const u8 o16[] = { offsetof(mNpc_Default_Data_c, cloth),
                                      offsetof(mNpc_Default_Data_c, catchphrase_str_idx) };
            swap_records(fix->data, fix->size, sizeof(mNpc_Default_Data_c), o16, 2, NULL, 0);
            break;
        }

        case PC_FIX_VTX: {
            static const u8 o16[] = { 0, 2, 4, 6, 8, 10 }; /* ob[3], flag, tc[2]; cn[4] are bytes */
            swap_records(fix->data, fix->size, 16, o16, 6, NULL, 0);
            break;
        }

        case PC_FIX_HOUSE_POS: {
            static const u8 o16[] = { offsetof(mMP_HousePos_c, fgblock_name) };
            swap_records(fix->data, fix->size, sizeof(mMP_HousePos_c), o16, 1, NULL, 0);
            break;
        }
    }
}

void pc_boot_fixup_data(void) {
    static int done = FALSE;
    int i;

    if (done) {
        return;
    }
    done = TRUE;

    for (i = 0; i < pc_data_fixup_count; i++) {
        apply_fixup(&pc_data_fixups[i]);
    }
    OSReport("[PC] swapped %d big-endian data blobs\n", pc_data_fixup_count);
}
