/* pc_ko_editor.c - input panel of the text editor on a Korean patch disc
 *
 * The patch replaced the editor's kana wheel with its own Hangul input and
 * reused the wheel's textures for it. The PC port takes text from the
 * keyboard instead (pc_typing.c), so this panel only says how to type. */
#include "pc_ko.h"

#include "pc_platform.h"
#include "pc_typing.h"
#include "graph.h"
#include "m_font.h"
#include "m_rcp.h"

#define PANEL_LEFT 76.0f
#define PANEL_TOP 150.0f
#define PANEL_WIDTH 168.0f
#define PANEL_HEIGHT 80.0f
#define TEXT_SCALE 0.75f
#define LINE_HEIGHT 15.0f

static void draw_rect(GRAPH* graph, f32 x, f32 y, f32 w, f32 h, int r, int g, int b, int a) {
    Gfx* gfx;

    OPEN_DISP(graph);
    gfx = NOW_POLY_OPA_DISP;
    gDPPipeSync(gfx++);
    gDPSetOtherMode(gfx++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    gfx = gfx_gSPTextureRectangle1(gfx, (int)(x * 4.0f), (int)(y * 4.0f), (int)((x + w) * 4.0f),
                                   (int)((y + h) * 4.0f), 0, 0, 0, 0, 0);
    gDPPipeSync(gfx++);
    SET_POLY_OPA_DISP(gfx);
    CLOSE_DISP(graph);
}

static void draw_text(GAME* game, const char* text, f32 x, f32 y, int r, int g, int b, f32 scale) {
    u8 buf[64];
    int len = pc_ko_encode_utf8(text, buf, sizeof(buf));

    mFont_SetLineStrings(game, buf, len, x, y, r, g, b, 255, FALSE, FALSE, scale, scale, mFont_MODE_POLY);
}

void pc_ko_editor_draw(GAME* game, f32 x, f32 y) {
    static const char* const help[] = {
        "한/영    오른쪽 Alt",
        "Enter    줄바꿈/결정",
        "Ctrl+Enter  끝내기",
    };
    f32 left = PANEL_LEFT + x;
    f32 top = PANEL_TOP - y;
    const char* mode;
    int i;

    draw_rect(game->graph, left, top, PANEL_WIDTH, PANEL_HEIGHT, 40, 60, 110, 200);

    if (!g_pc_typing_mode) {
        mode = "키보드 입력: Tab";
    } else if (pc_typing_korean()) {
        mode = "한글 입력";
    } else {
        mode = "영문 입력";
    }
    draw_text(game, mode, left + 8.0f, top + 6.0f, 255, 235, 120, 1.0f);
    for (i = 0; i < (int)(sizeof(help) / sizeof(help[0])); i++) {
        draw_text(game, help[i], left + 8.0f, top + 28.0f + LINE_HEIGHT * i, 235, 235, 235, TEXT_SCALE);
    }
}
