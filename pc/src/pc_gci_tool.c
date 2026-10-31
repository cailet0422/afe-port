/* afe_gci - bring GameCube memory card saves (.gci) of Doubutsu no Mori e+
 * into the PC port's save folder, and take them back out.
 *
 *   afe_gci info   FILE.gci
 *   afe_gci import FILE.gci [--card DIR] [--force]
 *   afe_gci export land|famicom [--card DIR] [-o FILE.gci] [--template FILE.gci]
 *
 * DIR is the PC port's card folder (default save/card_a). See docs/dev_setup.md. */
#include "pc_gci.h"
#include "pc_save_swap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CARD_EPOCH_OFFSET 946684800u /* 1970-01-01 to 2000-01-01 */

static int read_file(const char* path, uint8_t** data, size_t* size) {
    FILE* f = fopen(path, "rb");
    long n;

    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    *data = (uint8_t*)malloc(n ? (size_t)n : 1);
    if (!*data || fread(*data, 1, (size_t)n, f) != (size_t)n) {
        free(*data);
        fclose(f);
        return -1;
    }
    fclose(f);
    *size = (size_t)n;
    return 0;
}

/* Written to a temporary file first, then moved into place */
static int write_file(const char* path, const uint8_t* data, size_t size) {
    char tmp[1024];
    FILE* f;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f) return -1;
    if (fwrite(data, 1, size, f) != size || fclose(f) != 0) {
        remove(tmp);
        return -1;
    }
    remove(path); /* rename does not replace on Windows */
    if (rename(tmp, path) != 0) return -1;
    return 0;
}

static int exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static const char* yesno(int v) {
    return v ? "valid" : "INVALID";
}

static void usage(void) {
    fprintf(stderr,
            "usage:\n"
            "  afe_gci info   FILE.gci\n"
            "  afe_gci import FILE.gci [--card DIR] [--force]\n"
            "  afe_gci export land|famicom [--card DIR] [-o FILE.gci] [--template FILE.gci]\n"
            "DIR: the PC port's card folder (default save/card_a)\n");
}

static int cmd_info(const char* path) {
    uint8_t* gci;
    size_t size;
    pc_gci_dentry_t d;
    uint8_t* out;
    size_t out_size;
    pc_gci_info_t inf;
    char err[256];
    int rc;

    if (read_file(path, &gci, &size) != 0) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    if (size < PC_GCI_HEADER_SIZE) {
        fprintf(stderr, "%s: too small for a GCI\n", path);
        free(gci);
        return 1;
    }
    pc_gci_parse_dentry(gci, &d);
    printf("game %.4s%.2s, file %s, %u blocks, data at 0x%X, permission 0x%02X\n", d.game, d.company, d.filename,
           d.length, pc_gci_data_offset(&d), d.permission);
    rc = pc_gci_import(gci, size, &out, &out_size, &inf, err, sizeof(err));
    if (rc == PC_GCI_OK || inf.kind != PC_GCI_KIND_UNKNOWN) {
        if (inf.kind == PC_GCI_KIND_LAND) {
            printf("town data %s, backup %s, diary %s\n", yesno(inf.save_ok), yesno(inf.backup_ok), yesno(inf.diary_ok));
        } else if (inf.kind == PC_GCI_KIND_FAMICOM) {
            printf("Famicom save %s\n", yesno(inf.famicom_ok));
        }
    }
    if (rc != PC_GCI_OK) printf("cannot import: %s\n", err);
    free(out);
    free(gci);
    return rc == PC_GCI_OK ? 0 : 1;
}

static int cmd_import(const char* path, const char* card, int force) {
    uint8_t* gci;
    size_t size;
    uint8_t* out;
    size_t out_size;
    pc_gci_info_t inf;
    char err[256], dst[1024], bak[1100];
    int rc;

    if (read_file(path, &gci, &size) != 0) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    rc = pc_gci_import(gci, size, &out, &out_size, &inf, err, sizeof(err));
    free(gci);
    if (rc != PC_GCI_OK) {
        fprintf(stderr, "%s: %s\n", path, err);
        return 1;
    }
    snprintf(dst, sizeof(dst), "%s/%s", card,
             inf.kind == PC_GCI_KIND_LAND ? PC_GCI_PC_LAND_FILE : PC_GCI_PC_FAMICOM_FILE);
    if (exists(dst)) {
        if (!force) {
            fprintf(stderr, "%s already exists; --force replaces it (the old file is kept as .bak)\n", dst);
            free(out);
            return 1;
        }
        snprintf(bak, sizeof(bak), "%s.bak", dst);
        remove(bak);
        if (rename(dst, bak) != 0) {
            fprintf(stderr, "cannot keep the old %s as %s\n", dst, bak);
            free(out);
            return 1;
        }
        printf("old file kept as %s\n", bak);
    }
    if (write_file(dst, out, out_size) != 0) {
        fprintf(stderr, "cannot write %s (does the folder exist?)\n", dst);
        free(out);
        return 1;
    }
    free(out);
    if (inf.kind == PC_GCI_KIND_LAND) {
        printf("town data %s, backup %s, diary %s\n", yesno(inf.save_ok), yesno(inf.backup_ok), yesno(inf.diary_ok));
    }
    printf("wrote %s\n", dst);
    return 0;
}

static int cmd_export(const char* what, const char* card, const char* out_path, const char* template_path) {
    pc_gci_kind_t kind;
    char src[1024], def_out[64];
    uint8_t *pc, *tpl = NULL, *gci;
    size_t pc_size, tpl_size = 0, gci_size;
    pc_gci_info_t inf;
    char err[256];
    time_t now = time(NULL);
    int rc;

    if (strcmp(what, "land") == 0) {
        kind = PC_GCI_KIND_LAND;
    } else if (strcmp(what, "famicom") == 0) {
        kind = PC_GCI_KIND_FAMICOM;
    } else {
        usage();
        return 2;
    }
    snprintf(src, sizeof(src), "%s/%s", card, kind == PC_GCI_KIND_LAND ? PC_GCI_PC_LAND_FILE : PC_GCI_PC_FAMICOM_FILE);
    if (!out_path) {
        snprintf(def_out, sizeof(def_out), "01-GAEJ-%s.gci", kind == PC_GCI_KIND_LAND ? PC_GCI_LAND_NAME : PC_GCI_FAMICOM_NAME);
        out_path = def_out;
    }
    if (read_file(src, &pc, &pc_size) != 0) {
        fprintf(stderr, "cannot read %s\n", src);
        return 1;
    }
    if (template_path && read_file(template_path, &tpl, &tpl_size) != 0) {
        fprintf(stderr, "cannot read %s\n", template_path);
        free(pc);
        return 1;
    }
    rc = pc_gci_export(kind, pc, pc_size, tpl, tpl_size,
                       (uint32_t)now > CARD_EPOCH_OFFSET ? (uint32_t)now - CARD_EPOCH_OFFSET : 0, &gci, &gci_size, &inf,
                       err, sizeof(err));
    free(pc);
    free(tpl);
    if (rc != PC_GCI_OK) {
        fprintf(stderr, "%s: %s\n", src, err);
        return 1;
    }
    if (write_file(out_path, gci, gci_size) != 0) {
        fprintf(stderr, "cannot write %s\n", out_path);
        free(gci);
        return 1;
    }
    free(gci);
    if (kind == PC_GCI_KIND_LAND) {
        printf("town data %s, backup %s, diary %s\n", yesno(inf.save_ok), yesno(inf.backup_ok), yesno(inf.diary_ok));
    }
    printf("wrote %s\n", out_path);
    return 0;
}

int main(int argc, char** argv) {
    const char* card = "save/card_a";
    const char* out = NULL;
    const char* tpl = NULL;
    const char* pos[2] = { NULL, NULL };
    int npos = 0, force = 0, i;

    if (argc < 2) {
        usage();
        return 2;
    }
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--card") == 0 && i + 1 < argc) {
            card = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out = argv[++i];
        } else if (strcmp(argv[i], "--template") == 0 && i + 1 < argc) {
            tpl = argv[++i];
        } else if (strcmp(argv[i], "--force") == 0) {
            force = 1;
        } else if (argv[i][0] != '-' && npos < 2) {
            pos[npos++] = argv[i];
        } else {
            usage();
            return 2;
        }
    }
    if (strcmp(argv[1], "info") == 0 && npos == 1) return cmd_info(pos[0]);
    if (strcmp(argv[1], "import") == 0 && npos == 1) return cmd_import(pos[0], card, force);
    if (strcmp(argv[1], "export") == 0 && npos == 1) return cmd_export(pos[0], card, out, tpl);
    usage();
    return 2;
}
