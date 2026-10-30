/* pc_data_fixups.h - table of big-endian ROM blobs swapped at boot (see gen_data_fixups.py) */
#ifndef PC_DATA_FIXUPS_H
#define PC_DATA_FIXUPS_H

typedef enum {
    PC_FIX_U16,          /* array of 16-bit values */
    PC_FIX_U32,          /* array of 32-bit values (also raw display lists) */
    PC_FIX_MOVE_ACTOR,   /* mFM_move_actor_data_c[] */
    PC_FIX_COMBO_INFO,   /* mFM_combo_info_c[] */
    PC_FIX_NPC_HOUSE,    /* mNpc_NpcHouseData_c[] */
    PC_FIX_NPC_DEFAULT,  /* mNpc_Default_Data_c[] */
    PC_FIX_HOUSE_POS,    /* mMP_HousePos_c[] */
    PC_FIX_VTX,          /* Vtx[]: 16-bit position/flag/texcoord, byte colors */
} PCDataFixupKind;

typedef struct {
    unsigned char* data;
    unsigned int size;
    PCDataFixupKind kind;
} PCDataFixup;

extern const PCDataFixup pc_data_fixups[];
extern const int pc_data_fixup_count;

#endif
