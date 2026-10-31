/* pc_save_swap.h - e+ save data between the PC port's byte order and the
 * GameCube's (memory card / GCI files).
 *
 * The PC port writes its saves in the host's order, little-endian on every
 * machine it builds for; the GameCube's are big-endian. The field list comes
 * from the e+ headers (pc/src/pc_save_swap_table.inc, made by
 * pc/tools/gen_save_swap.py). Checksums are recomputed in the target order.
 *
 * Only plain C types here: the GCI tool and the tests use it without the
 * decomp's headers. */
#ifndef PC_SAVE_SWAP_H
#define PC_SAVE_SWAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PC_SWAP_FROM_BE, /* GameCube -> PC */
    PC_SWAP_TO_BE,   /* PC -> GameCube */
} pc_swap_dir_t;

/* --- Layout of the files (checked against the e+ headers in pc_save_swap.c) --- */

/* DobutsunomoriE_MURA: card header + diary, town data, its backup copy, and an
 * area the game never writes (left as it is) */
#define PC_LAND_FILE_SIZE     0x72000u /* mCD_LAND_SAVE_SIZE */
#define PC_LAND_DIARY_OFS     0x1460u  /* MemcardHeader_c + 32 bytes */
#define PC_LAND_DIARY_SIZE    0xD1C0u  /* ALIGN_NEXT(sizeof(mCD_keep_diary_c), 32): what its checksum covers */
#define PC_LAND_SAVE_OFS      0x10000u /* mCD_MISC_SAVE_SIZE */
#define PC_LAND_SAVE_SIZE     0x2E000u /* sizeof(Save): Save_t padded to card sectors */
#define PC_LAND_SAVE_T_SIZE   0x2D800u /* sizeof(Save_t) */
#define PC_LAND_BACKUP_OFS    (PC_LAND_SAVE_OFS + PC_LAND_SAVE_SIZE)
#define PC_LAND_SAVE_CHECKSUM 0x12u    /* offsetof(Save_t, save_check.checksum) */

/* DobutsunomoriE_F_SAVE (PC: DobutsunomoriP_F_SAVE.sav): 0x40-byte header and
 * 0x660 bytes per player */
#define PC_FAMICOM_HEADER_SIZE 0x40u
#define PC_FAMICOM_PLAYER_SIZE 0x660u
#define PC_FAMICOM_PLAYERS     4u
#define PC_FAMICOM_SAVE_SIZE   (PC_FAMICOM_HEADER_SIZE + PC_FAMICOM_PLAYER_SIZE * PC_FAMICOM_PLAYERS) /* 0x19C0 */
#define PC_FAMICOM_ID          "GAEJFSC\x1A"

/* --- Structures --- */

/* Swap the fields of one structure in place (sizeof(Save_t) bytes etc.) */
void pc_swap_save_t(uint8_t* save, pc_swap_dir_t dir);
void pc_swap_keep_diary(uint8_t* diary, pc_swap_dir_t dir);
void pc_swap_famicom_header(uint8_t* header, pc_swap_dir_t dir);

/* --- Checksums (m_flashrom.c: the u16 words of the block add up to 0) --- */

/* Sum of the block's u16 words, read big-endian or little-endian */
uint16_t pc_save_sum16(const uint8_t* data, size_t size, int big_endian);
/* Set the u16 at checksum_ofs so the block's words add up to 0 */
void pc_save_set_checksum(uint8_t* data, size_t size, size_t checksum_ofs, int big_endian);

/* --- Whole files --- */

typedef struct {
    /* checksums found valid in the source (before converting) */
    int save_ok;   /* town data */
    int backup_ok; /* its backup copy */
    int diary_ok;
    int famicom_ok; /* famicom: header and every player's data */
} pc_save_report_t;

/* Convert a town file (PC_LAND_FILE_SIZE bytes) in place. A part whose
 * checksum was right comes out with a right checksum in the other order; a
 * part whose checksum was wrong stays wrong (the game then uses the backup or
 * treats the town as broken, as it would have). Returns 0, or -1 if the size
 * is wrong. */
int pc_save_land_convert(uint8_t* data, size_t size, pc_swap_dir_t dir, pc_save_report_t* report);

/* Convert the Famicom common save (PC_FAMICOM_SAVE_SIZE bytes) in place. Its
 * checksums are byte sums, the same in either order. Returns 0, or -1 if the
 * size or the identifier is wrong. */
int pc_save_famicom_convert(uint8_t* data, size_t size, pc_swap_dir_t dir, pc_save_report_t* report);

/* Is it a valid Famicom common save, as famicom_save_data_check sees it
 * (identifier, header and player checksums), with the header's size field in
 * the given order? */
int pc_save_famicom_valid(const uint8_t* data, size_t size, int big_endian);

#ifdef __cplusplus
}
#endif

#endif /* PC_SAVE_SWAP_H */
