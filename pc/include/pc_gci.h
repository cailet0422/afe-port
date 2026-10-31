/* pc_gci.h - GameCube memory card files (.gci) for the e+ saves.
 *
 * A .gci is the card's 0x40-byte directory entry (big-endian) followed by the
 * file's blocks. This turns the town file (DobutsunomoriE_MURA) and the
 * Famicom common save (DobutsunomoriE_F_SAVE) into the PC port's files in
 * save/card_a and back; the data goes through pc_save_swap. */
#ifndef PC_GCI_H
#define PC_GCI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_GCI_HEADER_SIZE 0x40u
#define PC_GCI_BLOCK_SIZE  0x2000u

/* the names on the GameCube's card and in save/card_a */
#define PC_GCI_LAND_NAME       "DobutsunomoriE_MURA"
#define PC_GCI_FAMICOM_NAME    "DobutsunomoriE_F_SAVE"
#define PC_GCI_PC_LAND_FILE    "DobutsunomoriE_MURA"
#define PC_GCI_PC_FAMICOM_FILE "DobutsunomoriP_F_SAVE.sav"

typedef struct {
    char game[5];
    char company[3];
    uint8_t banner_format;
    char filename[33];
    uint32_t time; /* seconds since 2000-01-01 */
    uint32_t icon_addr;
    uint16_t icon_format;
    uint16_t icon_speed;
    uint8_t permission;
    uint8_t copy_times;
    uint16_t start_block;
    uint16_t length; /* blocks */
    uint32_t comment_addr;
} pc_gci_dentry_t;

typedef enum {
    PC_GCI_KIND_UNKNOWN,
    PC_GCI_KIND_LAND,
    PC_GCI_KIND_FAMICOM,
} pc_gci_kind_t;

void pc_gci_parse_dentry(const uint8_t* hdr, pc_gci_dentry_t* d);
void pc_gci_write_dentry(uint8_t* hdr, const pc_gci_dentry_t* d);

/* Where the data starts after the comment, banner and icons, as the card
 * library works out CARDStat.offsetData */
uint32_t pc_gci_data_offset(const pc_gci_dentry_t* d);

/* Results: 0, or a negative error with a message in err */
enum {
    PC_GCI_OK = 0,
    PC_GCI_ERR_FORMAT = -1, /* not a GCI of these files, or the wrong size */
    PC_GCI_ERR_DATA = -2,   /* the data is not what it should be */
    PC_GCI_ERR_MEMORY = -3,
};

typedef struct {
    pc_gci_kind_t kind;
    int save_ok, backup_ok, diary_ok; /* land: checksums valid in the GCI */
    int famicom_ok;                   /* famicom: checksums valid */
} pc_gci_info_t;

/* GCI -> the PC file's bytes (malloc'd, *out_size bytes) */
int pc_gci_import(const uint8_t* gci, size_t gci_size, uint8_t** out, size_t* out_size, pc_gci_info_t* info,
                  char* err, size_t err_size);

/* PC file -> GCI (malloc'd). time: seconds since 2000-01-01 for the entry.
 * template_gci (may be NULL): for the Famicom save, a GCI of the same file
 * whose comment, banner and icon are kept; without one the GCI only has a
 * comment (the PC port does not keep the Famicom save's picture). */
int pc_gci_export(pc_gci_kind_t kind, const uint8_t* pc, size_t pc_size, const uint8_t* template_gci,
                  size_t template_size, uint32_t time, uint8_t** out, size_t* out_size, pc_gci_info_t* info,
                  char* err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif /* PC_GCI_H */
