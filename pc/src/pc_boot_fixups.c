/* pc_boot_fixups.c - byte-swap big-endian ROM data that the game reads as structs.
 *
 * Most extracted assets are either raw bytes (textures, consumed by the
 * big-endian-aware texture decoder) or converted to native C initializers
 * (vertices, palettes). A few u8[] blobs are reinterpreted as display lists,
 * TLUTs or structs by game code; those need swapping once at boot.
 * Called from boot.c before the game starts.
 */
#include "pc_platform.h"
#include "pc_bswap.h"

void pc_boot_fixup_data(void) {
}
