/* Unit tests for the e+ save conversion (pc/src/pc_save_swap.c, pc_gci.c).
 *
 * All data is made up here: a town and a Famicom save filled with chosen
 * values or pseudo-random bytes. No game data. */
#include "m_common_data.h"
#include "m_card.h"
#include "m_quest.h"
#include "m_event.h"
#include "Famicom/famicom.h"

#include "pc_gci.h"
#include "pc_save_swap.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

/* --- sizes and offsets against the e+ headers (their comments) --- */

_Static_assert(sizeof(Save_t) == 0x2D800, "Save_t");
_Static_assert(sizeof(Save) == 0x2E000, "Save");
_Static_assert(offsetof(Save_t, save_check) == 0x00000, "save_check");
_Static_assert(offsetof(Save_t, land_info) == 0x00014, "land_info");
_Static_assert(offsetof(Save_t, private_data) == 0x001C0, "private_data");
_Static_assert(offsetof(Save_t, noticeboard) == 0x09C40, "noticeboard");
_Static_assert(offsetof(Save_t, homes) == 0x0A340, "homes");
_Static_assert(offsetof(Save_t, fg) == 0x184C0, "fg");
_Static_assert(offsetof(Save_t, combi_table) == 0x1C0C0, "combi_table");
_Static_assert(offsetof(Save_t, animals) == 0x1C150, "animals");
_Static_assert(offsetof(Save_t, shop) == 0x222EC, "shop");
_Static_assert(offsetof(Save_t, event_save_data) == 0x223E0, "event_save_data");
_Static_assert(offsetof(Save_t, add_npc) == 0x24490, "add_npc");
_Static_assert(offsetof(Save_t, travel_hard_time) == 0x2D670, "travel_hard_time");
_Static_assert(offsetof(Save_t, rnd_hash) == 0x2D7E8, "rnd_hash");
_Static_assert(sizeof(Animal_c) == 0x680, "Animal_c");
_Static_assert(sizeof(Anmmem_c) == 0xC8, "Anmmem_c");
_Static_assert(sizeof(mFRm_chk_t) == 0x14, "mFRm_chk_t");
_Static_assert(offsetof(mFRm_chk_t, checksum) == 0x12, "checksum");
_Static_assert(sizeof(Island_c) == 0x1620, "Island_c");
_Static_assert(offsetof(Island_c, animal) == 0x0F20, "Island_c.animal");
_Static_assert(sizeof(mQst_base_c) == 0xC, "mQst_base_c");
_Static_assert(sizeof(mHm_rmsz_c) == 6, "mHm_rmsz_c");
_Static_assert(sizeof(FamicomSaveDataHeader) == 0x40, "FamicomSaveDataHeader");
_Static_assert(sizeof(MemcardHeader_c) == 0x1440 && mCD_SAVE_DATA_OFS == 0x1440, "card header");

/* --- the game's checksum functions (m_flashrom.c), copied to check against --- */

static u16 game_ReturnCheckSum(u16* data, int size) {
    u16 checksum = 0;
    if ((size & 1) == 0) {
        while (size != 0) {
            checksum += *data;
            data++;
            size -= sizeof(u16);
        }
    }
    return checksum;
}

static u16 game_GetFlatCheckSum(u16* data, int size, u16 now_checksum) {
    u16 checksum = game_ReturnCheckSum(data, size) - now_checksum;
    return (u16)(((~checksum & 0xFFFF) + 1) & 0xFFFF);
}

/* --- helpers --- */

static u32 rng_state = 12345;

static u8 rnd8(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (u8)(rng_state >> 16);
}

static void fill_random(u8* p, size_t n) {
    while (n--) *p++ = rnd8();
}

static u16 be16(const u8* p) {
    return (u16)((p[0] << 8) | p[1]);
}

static u32 be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u64 be64(const u8* p) {
    return ((u64)be32(p) << 32) | be32(p + 4);
}

#define AT(base, type, field) ((const u8*)(base) + offsetof(type, field))

/* --- checksums --- */

static void test_checksum(void) {
    static u8 block[0x2E000];
    Save_t* save = (Save_t*)block;

    fill_random(block, sizeof(block));

    /* the PC game's rule, on its own (little-endian) data */
    save->save_check.checksum = game_GetFlatCheckSum((u16*)block, sizeof(block), save->save_check.checksum);
    CHECK(game_ReturnCheckSum((u16*)block, sizeof(block)) == 0);
    CHECK(pc_save_sum16(block, sizeof(block), 0) == 0);

    /* pc_save_set_checksum gives the same value the game does */
    {
        u16 game_value = save->save_check.checksum;
        save->save_check.checksum ^= 0x5A5A;
        pc_save_set_checksum(block, sizeof(block), PC_LAND_SAVE_CHECKSUM, 0);
        CHECK(save->save_check.checksum == game_value);
    }

    /* the GameCube's: the same sum over big-endian words */
    pc_save_set_checksum(block, sizeof(block), PC_LAND_SAVE_CHECKSUM, 1);
    CHECK(pc_save_sum16(block, sizeof(block), 1) == 0);

    /* the sums differ between the orders: converting must recompute it */
    pc_swap_save_t(block, PC_SWAP_FROM_BE);
    CHECK(pc_save_sum16(block, sizeof(block), 0) != 0 || pc_save_sum16(block, sizeof(block), 1) != 0);
}

/* --- field by field: a town made in the PC's layout, converted to the GameCube's --- */

static void test_fields(void) {
    static u8 buf[sizeof(Save_t)];
    Save_t* s = (Save_t*)buf;
    const u8* b = buf;
    Animal_c* a;
    Animal_c* isl;
    mHm_hs_c* home;

    memset(buf, 0, sizeof(buf));
    s->save_check.version = 0x01020304;
    s->save_check.code = 0xA1B2C3D4;
    s->save_check.time.year = 0x07E4;
    s->save_check.time.month = 8;
    s->land_info.id = 0x30C1;
    s->land_info.name[0] = 0x41;
    s->scene_no = 0x11223344;
    s->copy_protect = 0xBEEF;
    s->private_data[1].inventory.wallet = 0x00007530;
    s->private_data[1].inventory.pockets[3] = 0x2239;
    s->private_data[1].player_ID.player_id = 0xF0A3;
    s->melody = 0x0123456789ABCDEFull;
    s->time_delta = -2;
    s->deposit[5][7] = 0x8001;
    s->fruit = 0x2800;
    s->event_save_common.bridge_flags.raw = 0x85;

    /* bit-fields: the GameCube packs them from the most significant bit */
    home = &s->homes[2];
    home->size_info.size = 5;
    home->size_info.next_size = 3;
    home->size_info.statue_rank = 2;
    home->size_info.upgrade_order_date.year = 0x07E5;

    a = &s->animals[3];
    a->id.npc_id = 0xE041;
    a->id.land_id = 0x30C1;
    a->contest_quest.base.quest_type = mQst_QUEST_TYPE_CONTEST;
    a->contest_quest.base.quest_kind = mQst_CONTEST_KIND_LETTER;
    a->contest_quest.base.progress = 9;
    a->contest_quest.base.time_limit.sec = 0x3B;
    a->contest_quest.base.time_limit.min = 0x2A;
    a->contest_quest.base.time_limit.year = 0x07E4;
    a->contest_quest.type = mQst_CONTEST_DATA_LETTER;
    a->contest_quest.info.letter_data.present = 0x1234;
    a->contest_quest.info.letter_data.score = 7;
    a->memories[0].memuni.land.id = 0x4455;
    a->memories[0].saved_town_tune = 0x1122334455667788ull;
    a->anmuni.previous_land_id = 0x6677;

    s->animals[4].contest_quest.type = mQst_CONTEST_DATA_FLOWER;
    s->animals[4].contest_quest.info.flower_data.flowers_requested = 0x0C;

    /* the islander: its unions hold the island members */
    isl = &s->homes[1].island.animal;
    isl->memories[2].memuni.island.check = 0xCAFEBABE;
    isl->memories[2].memuni.island.have_bitfield = 0x0F0E;
    isl->anmuni.island_ftr[1] = 0x1A2B;

    /* a broker event (mEv_special_u by kind) */
    s->event_save_data.special.kind = mEv_SPNPC_BROKER;
    s->event_save_data.special.event.broker.used = 0x01020304;
    s->event_save_data.special.event.broker.items[2] = 0x3344;

    /* an errand of each kind (mQst_errand_info_u by errand_type) */
    s->private_data[0].errands[0].errand_type = mQst_ERRAND_TYPE_FIRST_JOB;
    s->private_data[0].errands[0].pockets_idx = 0x0B;
    s->private_data[0].errands[0].info.first_job.used_ids[1].npc_id = 0xE0C9;
    s->private_data[0].errands[0].info.first_job.used_num = 0x45;
    s->private_data[0].errands[0].info.first_job.wrong_cloth = 1;
    s->private_data[0].errands[1].errand_type = mQst_ERRAND_TYPE_CHAIN;
    s->private_data[0].errands[1].info.chain.used_ids[2].npc_id = 0xE082;
    s->private_data[0].errands[1].info.chain.used_num = 3;

    pc_swap_save_t(buf, PC_SWAP_TO_BE);

    CHECK(be32(AT(b, Save_t, save_check.version)) == 0x01020304);
    CHECK(be32(AT(b, Save_t, save_check.code)) == 0xA1B2C3D4);
    CHECK(be16(AT(b, Save_t, save_check.time.year)) == 0x07E4);
    CHECK(*AT(b, Save_t, save_check.time.month) == 8);
    CHECK(be16(AT(b, Save_t, land_info.id)) == 0x30C1);
    CHECK(*AT(b, Save_t, land_info.name) == 0x41);
    CHECK(be32(AT(b, Save_t, scene_no)) == 0x11223344);
    CHECK(be16(AT(b, Save_t, copy_protect)) == 0xBEEF);
    CHECK(be32(AT(b, Save_t, private_data[1].inventory.wallet)) == 0x7530);
    CHECK(be16(AT(b, Save_t, private_data[1].inventory.pockets[3])) == 0x2239);
    CHECK(be16(AT(b, Save_t, private_data[1].player_ID.player_id)) == 0xF0A3);
    CHECK(be64(AT(b, Save_t, melody)) == 0x0123456789ABCDEFull);
    CHECK(be64(AT(b, Save_t, time_delta)) == 0xFFFFFFFFFFFFFFFEull);
    CHECK(be16(AT(b, Save_t, deposit[5][7])) == 0x8001);
    CHECK(be16(AT(b, Save_t, fruit)) == 0x2800);
    CHECK(*AT(b, Save_t, event_save_common.bridge_flags) == 0x85); /* the game writes it as a byte */

    /* size:3 next_size:3 statue_rank:2, from the top bit */
    CHECK(*AT(b, Save_t, homes[2].size_info.upgrade_order_date.year) == 0x07);
    CHECK(b[offsetof(Save_t, homes[2].size_info) + 4] == ((5 << 5) | (3 << 2) | 2));

    CHECK(be16(AT(b, Save_t, animals[3].id.npc_id)) == 0xE041);
    CHECK(be16(AT(b, Save_t, animals[3].id.land_id)) == 0x30C1);
    /* quest_type:2 quest_kind:6 | time_limit_enabled:1 progress:4 give_reward:1 unused:2 */
    CHECK(b[offsetof(Save_t, animals[3].contest_quest.base)] == ((mQst_QUEST_TYPE_CONTEST << 6) | mQst_CONTEST_KIND_LETTER));
    CHECK(b[offsetof(Save_t, animals[3].contest_quest.base) + 1] == (9 << 3));
    /* time_limit shares the bit-fields' u32 unit: its bytes must not move */
    CHECK(*AT(b, Save_t, animals[3].contest_quest.base.time_limit.sec) == 0x3B);
    CHECK(*AT(b, Save_t, animals[3].contest_quest.base.time_limit.min) == 0x2A);
    CHECK(be16(AT(b, Save_t, animals[3].contest_quest.base.time_limit.year)) == 0x07E4);
    CHECK(be16(AT(b, Save_t, animals[3].contest_quest.info.letter_data.present)) == 0x1234);
    CHECK(*AT(b, Save_t, animals[3].contest_quest.info.letter_data.score) == 7);
    CHECK(be16(AT(b, Save_t, animals[3].memories[0].memuni.land.id)) == 0x4455);
    CHECK(be64(AT(b, Save_t, animals[3].memories[0].saved_town_tune)) == 0x1122334455667788ull);
    CHECK(be16(AT(b, Save_t, animals[3].anmuni.previous_land_id)) == 0x6677);
    /* a flower contest's count is a byte at offset 0: not swapped */
    CHECK(*AT(b, Save_t, animals[4].contest_quest.info.flower_data.flowers_requested) == 0x0C);

    CHECK(be32(AT(b, Save_t, homes[1].island.animal.memories[2].memuni.island.check)) == 0xCAFEBABE);
    CHECK(be16(AT(b, Save_t, homes[1].island.animal.memories[2].memuni.island.have_bitfield)) == 0x0F0E);
    CHECK(be16(AT(b, Save_t, homes[1].island.animal.anmuni.island_ftr[1])) == 0x1A2B);

    CHECK(be32(AT(b, Save_t, event_save_data.special.kind)) == mEv_SPNPC_BROKER);
    CHECK(be32(AT(b, Save_t, event_save_data.special.event.broker.used)) == 0x01020304);
    CHECK(be16(AT(b, Save_t, event_save_data.special.event.broker.items[2])) == 0x3344);

    /* pockets_idx:5 errand_type:3 */
    CHECK(b[offsetof(Save_t, private_data[0].errands[0].item) + 2] == ((0x0B << 3) | mQst_ERRAND_TYPE_FIRST_JOB));
    CHECK(be16(AT(b, Save_t, private_data[0].errands[0].info.first_job.used_ids[1].npc_id)) == 0xE0C9);
    /* used_num:7 wrong_cloth:1 */
    CHECK(b[offsetof(Save_t, private_data[0].errands[0].info.first_job.used_ids) + sizeof(s->private_data[0].errands[0].info.first_job.used_ids)] ==
          ((0x45 << 1) | 1));
    CHECK(be16(AT(b, Save_t, private_data[0].errands[1].info.chain.used_ids[2].npc_id)) == 0xE082);
    CHECK(*AT(b, Save_t, private_data[0].errands[1].info.chain.used_num) == 3);

    /* and back */
    pc_swap_save_t(buf, PC_SWAP_FROM_BE);
    CHECK(s->homes[2].size_info.size == 5 && s->homes[2].size_info.next_size == 3 && s->homes[2].size_info.statue_rank == 2);
    CHECK(s->animals[3].contest_quest.base.quest_kind == mQst_CONTEST_KIND_LETTER && s->animals[3].contest_quest.base.progress == 9);
    CHECK(s->animals[3].contest_quest.info.letter_data.present == 0x1234);
    CHECK(s->animals[4].contest_quest.info.flower_data.flowers_requested == 0x0C);
    CHECK(s->private_data[0].errands[0].info.first_job.used_num == 0x45);
    CHECK(s->homes[1].island.animal.memories[2].memuni.island.check == 0xCAFEBABE);
    CHECK(s->melody == 0x0123456789ABCDEFull);
}

/* --- bridge_flags: the game uses both the bit-fields and `raw` --- */

static void test_bridge_flags(void) {
    static Save_t s;

    /* as on the GameCube: bit 7 is used_all_locations, raw++ counts
     * locations_used up (m_event.c, ac_event_manager.c, ac_ev_soncho_talk) */
    s.event_save_common.bridge_flags.raw = 0x85;
    CHECK(s.event_save_common.bridge_flags.used_all_locations == 1);
    CHECK(s.event_save_common.bridge_flags.locations_used == 5);
    s.event_save_common.bridge_flags.raw = 0;
    s.event_save_common.bridge_flags.raw++;
    s.event_save_common.bridge_flags.raw++;
    CHECK(s.event_save_common.bridge_flags.locations_used == 2);
    CHECK(s.event_save_common.bridge_flags.used_all_locations == 0);
    s.event_save_common.bridge_flags.used_all_locations = 1;
    CHECK(s.event_save_common.bridge_flags.raw == 0x82);
}

/* --- the whole town file: PC -> GameCube -> PC is the same bytes --- */

static void make_land(u8* land) {
    fill_random(land, PC_LAND_FILE_SIZE);
    pc_save_set_checksum(land + PC_LAND_DIARY_OFS, PC_LAND_DIARY_SIZE, 0, 0);
    pc_save_set_checksum(land + PC_LAND_SAVE_OFS, PC_LAND_SAVE_SIZE, PC_LAND_SAVE_CHECKSUM, 0);
    pc_save_set_checksum(land + PC_LAND_BACKUP_OFS, PC_LAND_SAVE_SIZE, PC_LAND_SAVE_CHECKSUM, 0);
}

static void test_land_roundtrip(void) {
    static u8 orig[PC_LAND_FILE_SIZE], land[PC_LAND_FILE_SIZE];
    pc_save_report_t r;
    int seed;

    for (seed = 1; seed <= 3; seed++) {
        rng_state = (u32)seed * 7919u;
        make_land(orig);
        memcpy(land, orig, sizeof(land));

        CHECK(pc_save_land_convert(land, sizeof(land), PC_SWAP_TO_BE, &r) == 0);
        CHECK(r.save_ok && r.backup_ok && r.diary_ok);
        CHECK(pc_save_sum16(land + PC_LAND_SAVE_OFS, PC_LAND_SAVE_SIZE, 1) == 0);
        CHECK(pc_save_sum16(land + PC_LAND_BACKUP_OFS, PC_LAND_SAVE_SIZE, 1) == 0);
        CHECK(pc_save_sum16(land + PC_LAND_DIARY_OFS, PC_LAND_DIARY_SIZE, 1) == 0);
        /* the card header (comment, banner, icon) stays as it is */
        CHECK(memcmp(land, orig, PC_LAND_DIARY_OFS) == 0);
        CHECK(memcmp(land + PC_LAND_BACKUP_OFS + PC_LAND_SAVE_SIZE, orig + PC_LAND_BACKUP_OFS + PC_LAND_SAVE_SIZE,
                     PC_LAND_FILE_SIZE - PC_LAND_BACKUP_OFS - PC_LAND_SAVE_SIZE) == 0);

        CHECK(pc_save_land_convert(land, sizeof(land), PC_SWAP_FROM_BE, &r) == 0);
        CHECK(r.save_ok && r.backup_ok && r.diary_ok);
        CHECK(memcmp(land, orig, sizeof(land)) == 0);
    }

    /* a broken copy stays broken, and the other way round still works */
    rng_state = 99;
    make_land(orig);
    orig[PC_LAND_BACKUP_OFS + 0x100] ^= 0x40;
    memcpy(land, orig, sizeof(land));
    pc_save_land_convert(land, sizeof(land), PC_SWAP_TO_BE, &r);
    CHECK(r.save_ok && !r.backup_ok);
    CHECK(pc_save_sum16(land + PC_LAND_SAVE_OFS, PC_LAND_SAVE_SIZE, 1) == 0);
    CHECK(pc_save_sum16(land + PC_LAND_BACKUP_OFS, PC_LAND_SAVE_SIZE, 1) != 0);
    pc_save_land_convert(land, sizeof(land), PC_SWAP_FROM_BE, &r);
    CHECK(memcmp(land, orig, sizeof(land)) == 0);

    CHECK(pc_save_land_convert(land, sizeof(land) - 1, PC_SWAP_TO_BE, &r) == -1);
}

/* --- the Famicom common save --- */

static u8 byte_sum(const u8* p, size_t n) {
    u8 s = 0;
    while (n--) s += *p++;
    return s;
}

/* as famicom_save_data_setup (famicom.cpp) */
static void make_famicom(u8* f) {
    FamicomSaveDataHeader* h = (FamicomSaveDataHeader*)f;
    u32 i;

    fill_random(f, PC_FAMICOM_SAVE_SIZE);
    memcpy(h->name, PC_FAMICOM_ID, 8);
    h->_08 = 1;
    h->_09 = 0;
    h->headerSize = FAMICOM_SAVE_HEADER_SIZE;
    h->size = PC_FAMICOM_PLAYER_SIZE;
    h->no_save = 0;
    h->checksum -= byte_sum(f, FAMICOM_SAVE_HEADER_SIZE);
    for (i = 0; i < PC_FAMICOM_PLAYERS; i++) {
        u8* d = f + FAMICOM_SAVE_HEADER_SIZE + i * PC_FAMICOM_PLAYER_SIZE;
        d[0] = 'D';
        d[1] = '1';
        d[3] = (u8)i;
        d[2] -= byte_sum(d, PC_FAMICOM_PLAYER_SIZE);
    }
}

static void test_famicom(void) {
    static u8 orig[PC_FAMICOM_SAVE_SIZE], f[PC_FAMICOM_SAVE_SIZE];
    pc_save_report_t r;

    rng_state = 4242;
    make_famicom(orig);
    CHECK(pc_save_famicom_valid(orig, sizeof(orig), 0));
    memcpy(f, orig, sizeof(f));
    CHECK(pc_save_famicom_convert(f, sizeof(f), PC_SWAP_TO_BE, &r) == 0 && r.famicom_ok);
    CHECK(be16(f + offsetof(FamicomSaveDataHeader, size)) == PC_FAMICOM_PLAYER_SIZE);
    CHECK(pc_save_famicom_valid(f, sizeof(f), 1));
    /* the players' data is bytes for the emulator: unchanged */
    CHECK(memcmp(f + PC_FAMICOM_HEADER_SIZE, orig + PC_FAMICOM_HEADER_SIZE, sizeof(f) - PC_FAMICOM_HEADER_SIZE) == 0);
    CHECK(pc_save_famicom_convert(f, sizeof(f), PC_SWAP_FROM_BE, &r) == 0 && r.famicom_ok);
    CHECK(memcmp(f, orig, sizeof(f)) == 0);

    f[PC_FAMICOM_HEADER_SIZE + 5] ^= 1;
    CHECK(!pc_save_famicom_valid(f, sizeof(f), 0));
    CHECK(pc_save_famicom_convert(f, sizeof(f) - 2, PC_SWAP_TO_BE, &r) == -1);
}

/* --- GCI files --- */

static void test_gci(void) {
    static u8 land[PC_LAND_FILE_SIZE], fam[PC_FAMICOM_SAVE_SIZE];
    u8 *gci, *back, *gci2;
    size_t gci_size, back_size, gci2_size;
    pc_gci_info_t inf;
    pc_gci_dentry_t d;
    char err[256];

    /* town */
    rng_state = 777;
    make_land(land);
    CHECK(pc_gci_export(PC_GCI_KIND_LAND, land, sizeof(land), NULL, 0, 649988226, &gci, &gci_size, &inf, err,
                        sizeof(err)) == PC_GCI_OK);
    CHECK(gci_size == PC_GCI_HEADER_SIZE + PC_LAND_FILE_SIZE);
    pc_gci_parse_dentry(gci, &d);
    CHECK(memcmp(d.game, "GAEJ", 4) == 0 && memcmp(d.company, "01", 2) == 0);
    CHECK(strcmp(d.filename, "DobutsunomoriE_MURA") == 0);
    CHECK(d.length == 57 && d.time == 649988226);
    CHECK(d.permission == (CARD_ATTR_PUBLIC | CARD_ATTR_NO_COPY | CARD_ATTR_NO_MOVE));
    CHECK(d.comment_addr == 0 && d.icon_addr == CARD_COMMENT_SIZE);
    /* the data starts where the game reads it after the picture */
    CHECK(pc_gci_data_offset(&d) == mCD_SAVE_DATA_OFS);
    CHECK(pc_save_sum16(gci + PC_GCI_HEADER_SIZE + PC_LAND_SAVE_OFS, PC_LAND_SAVE_SIZE, 1) == 0);

    CHECK(pc_gci_import(gci, gci_size, &back, &back_size, &inf, err, sizeof(err)) == PC_GCI_OK);
    CHECK(inf.kind == PC_GCI_KIND_LAND && inf.save_ok && inf.backup_ok && inf.diary_ok);
    CHECK(back_size == sizeof(land) && memcmp(back, land, sizeof(land)) == 0);
    free(back);

    /* wrong game, wrong length */
    gci[3] = 'E';
    CHECK(pc_gci_import(gci, gci_size, &back, &back_size, &inf, err, sizeof(err)) == PC_GCI_ERR_FORMAT);
    gci[3] = 'J';
    CHECK(pc_gci_import(gci, gci_size - PC_GCI_BLOCK_SIZE, &back, &back_size, &inf, err, sizeof(err)) == PC_GCI_ERR_FORMAT);
    /* both copies broken */
    gci[PC_GCI_HEADER_SIZE + PC_LAND_SAVE_OFS + 0x40] ^= 1;
    gci[PC_GCI_HEADER_SIZE + PC_LAND_BACKUP_OFS + 0x40] ^= 1;
    CHECK(pc_gci_import(gci, gci_size, &back, &back_size, &inf, err, sizeof(err)) == PC_GCI_ERR_DATA);
    free(gci);

    /* Famicom, without a template: comment, then the data */
    rng_state = 31337;
    make_famicom(fam);
    CHECK(pc_gci_export(PC_GCI_KIND_FAMICOM, fam, sizeof(fam), NULL, 0, 1000, &gci, &gci_size, &inf, err,
                        sizeof(err)) == PC_GCI_OK);
    CHECK(gci_size == PC_GCI_HEADER_SIZE + PC_GCI_BLOCK_SIZE);
    pc_gci_parse_dentry(gci, &d);
    CHECK(strcmp(d.filename, "DobutsunomoriE_F_SAVE") == 0 && d.length == 1);
    CHECK(d.comment_addr == 0 && d.comment_addr != 0xFFFFFFFFu);
    CHECK(pc_gci_data_offset(&d) == CARD_COMMENT_SIZE);
    CHECK(memcmp(gci + PC_GCI_HEADER_SIZE + CARD_COMMENT_SIZE, PC_FAMICOM_ID, 8) == 0);
    CHECK(pc_save_famicom_valid(gci + PC_GCI_HEADER_SIZE + CARD_COMMENT_SIZE, PC_FAMICOM_SAVE_SIZE, 1));
    CHECK(pc_gci_import(gci, gci_size, &back, &back_size, &inf, err, sizeof(err)) == PC_GCI_OK);
    CHECK(inf.kind == PC_GCI_KIND_FAMICOM && inf.famicom_ok);
    CHECK(back_size == sizeof(fam) && memcmp(back, fam, sizeof(fam)) == 0);
    free(back);

    /* with a template: a made-up picture (C8 banner, two C8 icon frames) is kept */
    {
        static u8 tpl[PC_GCI_HEADER_SIZE + 2 * PC_GCI_BLOCK_SIZE];
        pc_gci_dentry_t t;
        u32 ofs;

        memset(&t, 0, sizeof(t));
        memcpy(t.game, "GAEJ", 4);
        memcpy(t.company, "01", 2);
        strcpy(t.filename, "DobutsunomoriE_F_SAVE");
        t.banner_format = CARD_STAT_BANNER_C8;
        t.icon_addr = CARD_COMMENT_SIZE;
        t.icon_format = CARD_STAT_ICON_C8 | (CARD_STAT_ICON_C8 << 2);
        t.icon_speed = CARD_STAT_SPEED_SLOW | (CARD_STAT_SPEED_SLOW << 2);
        t.length = 2;
        t.comment_addr = 0;
        pc_gci_write_dentry(tpl, &t);
        ofs = pc_gci_data_offset(&t);
        CHECK(ofs == 0x40 + 0xC00 + 0x200 + 2 * 0x400 + 0x200);
        fill_random(tpl + PC_GCI_HEADER_SIZE, ofs);
        CHECK(pc_gci_export(PC_GCI_KIND_FAMICOM, fam, sizeof(fam), tpl, sizeof(tpl), 2000, &gci2, &gci2_size, &inf, err,
                            sizeof(err)) == PC_GCI_OK);
        CHECK(gci2_size == PC_GCI_HEADER_SIZE + 2 * PC_GCI_BLOCK_SIZE);
        CHECK(memcmp(gci2 + PC_GCI_HEADER_SIZE, tpl + PC_GCI_HEADER_SIZE, ofs) == 0);
        CHECK(memcmp(gci2 + PC_GCI_HEADER_SIZE + ofs, PC_FAMICOM_ID, 8) == 0);
        CHECK(pc_gci_import(gci2, gci2_size, &back, &back_size, &inf, err, sizeof(err)) == PC_GCI_OK);
        CHECK(back_size == sizeof(fam) && memcmp(back, fam, sizeof(fam)) == 0);
        free(back);
        free(gci2);
    }
    free(gci);

    /* not one of these files */
    {
        u8 other[PC_GCI_HEADER_SIZE + PC_GCI_BLOCK_SIZE];
        pc_gci_dentry_t o;

        memset(other, 0, sizeof(other));
        memset(&o, 0, sizeof(o));
        memcpy(o.game, "GAEJ", 4);
        memcpy(o.company, "01", 2);
        strcpy(o.filename, "DobutsunomoriE_PL_0");
        o.length = 1;
        pc_gci_write_dentry(other, &o);
        CHECK(pc_gci_import(other, sizeof(other), &back, &back_size, &inf, err, sizeof(err)) == PC_GCI_ERR_FORMAT);
    }
}

int main(void) {
    test_checksum();
    test_fields();
    test_bridge_flags();
    test_land_roundtrip();
    test_famicom();
    test_gci();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("save swap: all tests passed\n");
    return 0;
}
