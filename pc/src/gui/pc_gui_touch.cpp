/* pc_gui_touch.cpp - the full touch mode's icons (pc_touch_full.h), drawn
 * with ImGui's draw lists over the game. The pictures are drawn here from
 * shapes: a satchel for the pockets, a folded map, an armchair, a gamepad,
 * the tools; nothing is taken from the game's own pictures. */
#include "pc_gui_internal.h"

extern "C" {
#include "pc_touch_full.h"
}

#include <math.h>

namespace gui {

static const float PI = 3.14159265f;

/* The theme: cream discs with a brown rim, as the menus */
static ImU32 Rgb(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}

static const ImU32 kInk = IM_COL32(0x5A, 0x44, 0x30, 0xFF);   /* lines */
static const ImU32 kWood = IM_COL32(0xA8, 0x74, 0x48, 0xFF);  /* handles */
static const ImU32 kMetal = IM_COL32(0x9C, 0xA6, 0xB0, 0xFF); /* blades */
static const ImU32 kGold = IM_COL32(0xF0, 0xC0, 0x38, 0xFF);

static void Line(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float t) {
    dl->AddLine(a, b, c, t);
}

static ImVec2 P(ImVec2 c, float r, float x, float y) {
    return ImVec2(c.x + x * r, c.y + y * r);
}

/* --- Pictures (c the centre, r the room for it) ------------------------- */

static void DrawMenu(ImDrawList* dl, ImVec2 c, float r) {
    float t = r * 0.16f;
    for (int i = -1; i <= 1; i++) {
        Line(dl, P(c, r, -0.55f, 0.42f * i), P(c, r, 0.55f, 0.42f * i), kInk, t);
    }
}

static void DrawSave(ImDrawList* dl, ImVec2 c, float r) {
    float t = r * 0.13f;
    /* a tray, and an arrow going down into it */
    dl->PathLineTo(P(c, r, -0.62f, 0.15f));
    dl->PathLineTo(P(c, r, -0.62f, 0.62f));
    dl->PathLineTo(P(c, r, 0.62f, 0.62f));
    dl->PathLineTo(P(c, r, 0.62f, 0.15f));
    dl->PathStroke(kInk, 0, t);
    Line(dl, P(c, r, 0.0f, -0.70f), P(c, r, 0.0f, 0.18f), Rgb(0x3C, 0x8C, 0x4C), t * 1.2f);
    dl->AddTriangleFilled(P(c, r, -0.34f, 0.0f), P(c, r, 0.34f, 0.0f), P(c, r, 0.0f, 0.40f), Rgb(0x3C, 0x8C, 0x4C));
}

static void DrawPad(ImDrawList* dl, ImVec2 c, float r) {
    ImU32 body = Rgb(0x6C, 0x5C, 0xB8);
    dl->AddRectFilled(P(c, r, -0.78f, -0.40f), P(c, r, 0.78f, 0.42f), body, r * 0.36f);
    /* a cross on the left, two buttons on the right */
    dl->AddRectFilled(P(c, r, -0.56f, -0.07f), P(c, r, -0.18f, 0.07f), IM_COL32_WHITE, r * 0.03f);
    dl->AddRectFilled(P(c, r, -0.44f, -0.20f), P(c, r, -0.30f, 0.20f), IM_COL32_WHITE, r * 0.03f);
    dl->AddCircleFilled(P(c, r, 0.30f, 0.10f), r * 0.11f, Rgb(0x60, 0xD0, 0x90));
    dl->AddCircleFilled(P(c, r, 0.52f, -0.10f), r * 0.11f, Rgb(0xF0, 0x70, 0x70));
}

static void DrawBag(ImDrawList* dl, ImVec2 c, float r) {
    ImU32 leather = Rgb(0xC8, 0x8A, 0x4C);
    ImU32 dark = Rgb(0x9A, 0x62, 0x30);
    /* the strap over the top */
    dl->PathArcTo(P(c, r, 0.0f, -0.18f), r * 0.50f, PI * 1.08f, PI * 1.92f);
    dl->PathStroke(dark, 0, r * 0.13f);
    /* the body and its flap */
    dl->AddRectFilled(P(c, r, -0.70f, -0.18f), P(c, r, 0.70f, 0.66f), leather, r * 0.22f);
    dl->AddRectFilled(P(c, r, -0.70f, -0.22f), P(c, r, 0.70f, 0.20f), dark, r * 0.22f, ImDrawFlags_RoundCornersTop);
    /* a leaf for the clasp */
    dl->AddCircleFilled(P(c, r, 0.0f, 0.22f), r * 0.15f, Rgb(0x58, 0xB0, 0x58));
    Line(dl, P(c, r, -0.08f, 0.30f), P(c, r, 0.08f, 0.14f), Rgb(0x2E, 0x70, 0x34), r * 0.05f);
}

static void DrawMap(ImDrawList* dl, ImVec2 c, float r) {
    /* three panels folded like a screen */
    ImVec2 top[4] = { P(c, r, -0.72f, -0.48f), P(c, r, -0.24f, -0.62f), P(c, r, 0.24f, -0.48f), P(c, r, 0.72f, -0.62f) };
    ImVec2 bot[4] = { P(c, r, -0.72f, 0.58f), P(c, r, -0.24f, 0.44f), P(c, r, 0.24f, 0.58f), P(c, r, 0.72f, 0.44f) };
    ImU32 shades[3] = { Rgb(0x9C, 0xD4, 0x8C), Rgb(0x84, 0xC0, 0x78), Rgb(0x9C, 0xD4, 0x8C) };
    for (int i = 0; i < 3; i++) {
        dl->AddQuadFilled(top[i], top[i + 1], bot[i + 1], bot[i], shades[i]);
    }
    for (int i = 0; i < 3; i++) {
        dl->AddQuad(top[i], top[i + 1], bot[i + 1], bot[i], kInk, r * 0.06f);
    }
    /* a river and a pin */
    dl->AddBezierCubic(P(c, r, -0.70f, 0.10f), P(c, r, -0.30f, -0.20f), P(c, r, 0.20f, 0.35f), P(c, r, 0.70f, 0.00f),
                       Rgb(0x50, 0x90, 0xE0), r * 0.09f);
    dl->AddCircleFilled(P(c, r, 0.10f, -0.22f), r * 0.17f, Rgb(0xE0, 0x48, 0x48));
    dl->AddTriangleFilled(P(c, r, -0.02f, -0.14f), P(c, r, 0.22f, -0.14f), P(c, r, 0.10f, 0.12f), Rgb(0xE0, 0x48, 0x48));
    dl->AddCircleFilled(P(c, r, 0.10f, -0.24f), r * 0.06f, IM_COL32_WHITE);
}

static void DrawChair(ImDrawList* dl, ImVec2 c, float r, ImU32 cloth = IM_COL32(0xE0, 0x84, 0x6C, 0xFF)) {
    ImU32 dark = Rgb(0xA8, 0x58, 0x48);
    dl->AddRectFilled(P(c, r, -0.48f, -0.62f), P(c, r, 0.48f, 0.10f), cloth, r * 0.18f);    /* back */
    dl->AddRectFilled(P(c, r, -0.62f, 0.02f), P(c, r, 0.62f, 0.36f), cloth, r * 0.10f);    /* seat */
    dl->AddRectFilled(P(c, r, -0.72f, -0.20f), P(c, r, -0.46f, 0.36f), dark, r * 0.10f);   /* arms */
    dl->AddRectFilled(P(c, r, 0.46f, -0.20f), P(c, r, 0.72f, 0.36f), dark, r * 0.10f);
    Line(dl, P(c, r, -0.54f, 0.36f), P(c, r, -0.54f, 0.64f), kWood, r * 0.12f);           /* legs */
    Line(dl, P(c, r, 0.54f, 0.36f), P(c, r, 0.54f, 0.64f), kWood, r * 0.12f);
}

static void DrawClose(ImDrawList* dl, ImVec2 c, float r) {
    float t = r * 0.18f;
    Line(dl, P(c, r, -0.45f, -0.45f), P(c, r, 0.45f, 0.45f), kInk, t);
    Line(dl, P(c, r, 0.45f, -0.45f), P(c, r, -0.45f, 0.45f), kInk, t);
}

static void DrawRotate(ImDrawList* dl, ImVec2 c, float r, int dir) {
    /* an arc with an arrow head: dir 1 turns right (clockwise) */
    float a0 = dir > 0 ? PI * 1.15f : PI * 1.85f;
    float a1 = dir > 0 ? PI * 2.55f : PI * 0.45f;
    dl->PathArcTo(c, r * 0.48f, a0, a1);
    dl->PathStroke(kInk, 0, r * 0.14f);
    ImVec2 tip(c.x + cosf(a1) * r * 0.48f, c.y + sinf(a1) * r * 0.48f);
    float tan_a = a1 + (dir > 0 ? PI * 0.5f : -PI * 0.5f);
    ImVec2 fwd(cosf(tan_a) * r * 0.26f, sinf(tan_a) * r * 0.26f);
    ImVec2 side(cosf(a1) * r * 0.20f, sinf(a1) * r * 0.20f);
    dl->AddTriangleFilled(ImVec2(tip.x + fwd.x, tip.y + fwd.y), ImVec2(tip.x + side.x, tip.y + side.y),
                          ImVec2(tip.x - side.x, tip.y - side.y), kInk);
}

static void DrawStore(ImDrawList* dl, ImVec2 c, float r) {
    /* the satchel, smaller, with an arrow going in */
    DrawBag(dl, P(c, r, 0.0f, 0.22f), r * 0.70f);
    Line(dl, P(c, r, 0.0f, -0.80f), P(c, r, 0.0f, -0.30f), Rgb(0x3C, 0x8C, 0x4C), r * 0.14f);
    dl->AddTriangleFilled(P(c, r, -0.26f, -0.38f), P(c, r, 0.26f, -0.38f), P(c, r, 0.0f, -0.10f), Rgb(0x3C, 0x8C, 0x4C));
}

static void DrawDone(ImDrawList* dl, ImVec2 c, float r) {
    dl->PathLineTo(P(c, r, -0.50f, 0.02f));
    dl->PathLineTo(P(c, r, -0.14f, 0.38f));
    dl->PathLineTo(P(c, r, 0.52f, -0.36f));
    dl->PathStroke(Rgb(0x2E, 0x7D, 0x3E), 0, r * 0.20f);
}

/* The tools: a handle and what is on it */
static void DrawTool(ImDrawList* dl, ImVec2 c, float r, int glyph, bool gold) {
    ImU32 metal = gold ? kGold : kMetal;
    ImU32 handle = gold ? Rgb(0xC8, 0x98, 0x28) : kWood;
    float t = r * 0.12f;

    switch (glyph) {
        case PC_GLYPH_NET: {
            Line(dl, P(c, r, -0.62f, 0.62f), P(c, r, 0.02f, -0.02f), handle, t);
            ImVec2 hc = P(c, r, 0.26f, -0.26f);
            dl->AddCircleFilled(hc, r * 0.38f, IM_COL32(0xFF, 0xFF, 0xFF, 0x90));
            for (int i = -1; i <= 1; i++) {
                Line(dl, P(hc, r, -0.30f, 0.16f * i), P(hc, r, 0.30f, 0.16f * i), Rgb(0x90, 0x90, 0x90), r * 0.03f);
                Line(dl, P(hc, r, 0.16f * i, -0.30f), P(hc, r, 0.16f * i, 0.30f), Rgb(0x90, 0x90, 0x90), r * 0.03f);
            }
            dl->AddCircle(hc, r * 0.38f, metal, 0, r * 0.08f);
            break;
        }
        case PC_GLYPH_AXE:
            Line(dl, P(c, r, -0.50f, 0.66f), P(c, r, 0.30f, -0.56f), handle, t * 1.2f);
            dl->AddQuadFilled(P(c, r, 0.04f, -0.62f), P(c, r, 0.62f, -0.30f), P(c, r, 0.48f, 0.06f),
                              P(c, r, -0.10f, -0.22f), metal);
            break;
        case PC_GLYPH_SHOVEL:
            Line(dl, P(c, r, 0.52f, -0.66f), P(c, r, -0.08f, 0.12f), handle, t);
            Line(dl, P(c, r, 0.38f, -0.70f), P(c, r, 0.66f, -0.56f), handle, t);
            dl->AddQuadFilled(P(c, r, -0.10f, -0.02f), P(c, r, 0.10f, 0.22f), P(c, r, -0.34f, 0.70f),
                              P(c, r, -0.60f, 0.40f), metal);
            break;
        case PC_GLYPH_ROD:
            dl->AddBezierQuadratic(P(c, r, -0.66f, 0.66f), P(c, r, 0.10f, -0.70f), P(c, r, 0.62f, -0.50f), handle, t);
            Line(dl, P(c, r, 0.62f, -0.50f), P(c, r, 0.56f, 0.30f), Rgb(0x60, 0x60, 0x60), r * 0.03f);
            dl->AddCircleFilled(P(c, r, 0.56f, 0.38f), r * 0.12f, Rgb(0xE0, 0x48, 0x48));
            dl->AddCircleFilled(P(c, r, -0.44f, 0.40f), r * 0.10f, metal);
            break;
        case PC_GLYPH_UMBRELLA:
            dl->PathArcTo(P(c, r, 0.0f, -0.08f), r * 0.66f, PI, 2.0f * PI);
            dl->PathFillConvex(Rgb(0x58, 0x9C, 0xE0));
            Line(dl, P(c, r, 0.0f, -0.08f), P(c, r, 0.0f, 0.52f), handle, t);
            dl->PathArcTo(P(c, r, -0.14f, 0.52f), r * 0.14f, 0.0f, PI);
            dl->PathStroke(handle, 0, t);
            break;
        case PC_GLYPH_BALLOON:
            dl->AddEllipseFilled(P(c, r, 0.0f, -0.22f), ImVec2(r * 0.44f, r * 0.52f), Rgb(0xE8, 0x50, 0x60));
            dl->AddBezierCubic(P(c, r, 0.0f, 0.30f), P(c, r, -0.20f, 0.45f), P(c, r, 0.20f, 0.55f), P(c, r, 0.0f, 0.72f),
                               kInk, r * 0.04f);
            break;
        case PC_GLYPH_PINWHEEL: {
            ImU32 cols[4] = { Rgb(0xF0, 0x60, 0x60), Rgb(0xF0, 0xD0, 0x40), Rgb(0x50, 0xA0, 0xE0), Rgb(0x60, 0xC0, 0x70) };
            ImVec2 hub = P(c, r, 0.0f, -0.18f);
            for (int i = 0; i < 4; i++) {
                float a = PI * 0.5f * i;
                ImVec2 o(cosf(a) * r * 0.46f, sinf(a) * r * 0.46f);
                ImVec2 s(cosf(a + PI * 0.5f) * r * 0.26f, sinf(a + PI * 0.5f) * r * 0.26f);
                dl->AddTriangleFilled(hub, ImVec2(hub.x + o.x, hub.y + o.y), ImVec2(hub.x + o.x + s.x, hub.y + o.y + s.y),
                                      cols[i]);
            }
            Line(dl, hub, P(c, r, 0.0f, 0.70f), handle, t);
            break;
        }
        case PC_GLYPH_FAN:
            dl->AddCircleFilled(P(c, r, 0.12f, -0.20f), r * 0.46f, Rgb(0x6C, 0xC0, 0x84));
            Line(dl, P(c, r, 0.12f, -0.20f), P(c, r, -0.50f, 0.62f), handle, t);
            break;
        case PC_GLYPH_FLOWER: {
            ImVec2 fc = P(c, r, 0.0f, -0.16f);
            Line(dl, fc, P(c, r, 0.0f, 0.70f), Rgb(0x48, 0x9C, 0x48), t);
            for (int i = 0; i < 5; i++) {
                float a = PI * 2.0f * i / 5.0f - PI * 0.5f;
                dl->AddCircleFilled(ImVec2(fc.x + cosf(a) * r * 0.26f, fc.y + sinf(a) * r * 0.26f), r * 0.20f,
                                    Rgb(0xF4, 0x8C, 0xB4));
            }
            dl->AddCircleFilled(fc, r * 0.15f, Rgb(0xF8, 0xD8, 0x40));
            break;
        }
        case PC_GLYPH_POPPER:
            dl->AddTriangleFilled(P(c, r, -0.56f, 0.62f), P(c, r, 0.02f, -0.16f), P(c, r, 0.24f, 0.10f),
                                  Rgb(0xE8, 0x80, 0x30));
            dl->AddCircleFilled(P(c, r, 0.30f, -0.40f), r * 0.08f, Rgb(0xE0, 0x48, 0x48));
            dl->AddCircleFilled(P(c, r, 0.52f, -0.10f), r * 0.07f, Rgb(0x50, 0xA0, 0xE0));
            dl->AddCircleFilled(P(c, r, 0.10f, -0.64f), r * 0.07f, Rgb(0x60, 0xC0, 0x70));
            break;
        case PC_GLYPH_FURNITURE:
            DrawChair(dl, c, r * 0.9f);
            break;
        case PC_GLYPH_NONE: {
            /* an open hand */
            ImU32 skin = Rgb(0xF6, 0xD2, 0xA8);
            dl->AddRectFilled(P(c, r, -0.36f, -0.10f), P(c, r, 0.34f, 0.58f), skin, r * 0.20f);
            for (int i = 0; i < 4; i++) {
                float x = -0.30f + 0.19f * i;
                dl->AddRectFilled(P(c, r, x, -0.60f + (i == 0 || i == 3 ? 0.12f : 0.0f)), P(c, r, x + 0.14f, 0.0f), skin,
                                  r * 0.07f);
            }
            dl->AddRectFilled(P(c, r, 0.26f, 0.02f), P(c, r, 0.62f, 0.18f), skin, r * 0.07f);
            dl->AddRect(P(c, r, -0.36f, -0.10f), P(c, r, 0.34f, 0.58f), Rgb(0xC8, 0x9C, 0x70), r * 0.20f, 0, r * 0.04f);
            break;
        }
        default: {
            /* a bundle: something else in hand */
            dl->AddRectFilled(P(c, r, -0.46f, -0.30f), P(c, r, 0.46f, 0.50f), Rgb(0xD8, 0xB8, 0x80), r * 0.12f);
            Line(dl, P(c, r, 0.0f, -0.30f), P(c, r, 0.0f, 0.50f), Rgb(0xC0, 0x50, 0x50), r * 0.10f);
            Line(dl, P(c, r, -0.46f, 0.10f), P(c, r, 0.46f, 0.10f), Rgb(0xC0, 0x50, 0x50), r * 0.10f);
            break;
        }
    }
}

/* --- The icons ------------------------------------------------------------ */

static void Disc(ImDrawList* dl, ImVec2 c, float r, bool pressed, bool active, float alpha = 1.0f) {
    int a = (int)(0xE8 * alpha);
    dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.08f), r, IM_COL32(0x30, 0x20, 0x10, (int)(0x40 * alpha)));
    dl->AddCircleFilled(c, r, pressed ? IM_COL32(0xE6, 0xD6, 0xB4, a) : IM_COL32(0xFF, 0xF8, 0xE8, a));
    dl->AddCircle(c, r, active ? IM_COL32(0x4C, 0xA8, 0x5C, a) : IM_COL32(0xB8, 0x98, 0x70, a), 0,
                  r * (active ? 0.14f : 0.07f));
}

static void DrawIcon(ImDrawList* dl, const PCTouchHudIcon& ic, float W, float H, float U) {
    ImVec2 c(ic.x * W, ic.y * H);
    float r = ic.r * U;
    float g = r * 0.78f; /* the picture inside the disc */
    bool side = ic.kind == PC_HUD_TOOL_PREV || ic.kind == PC_HUD_TOOL_NEXT;

    if (ic.kind == PC_HUD_FURN_SLOT) {
        dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r),
                          ic.active ? IM_COL32(0xD8, 0xF0, 0xC8, 0xF0) : IM_COL32(0xFF, 0xF8, 0xE8, 0xE8), r * 0.25f);
        dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r),
                    ic.active ? IM_COL32(0x4C, 0xA8, 0x5C, 0xFF) : IM_COL32(0xB8, 0x98, 0x70, 0xFF), r * 0.25f, 0,
                    r * (ic.active ? 0.12f : 0.06f));
        DrawChair(dl, c, g * 0.9f);
        return;
    }
    Disc(dl, c, r, ic.pressed != 0, ic.active != 0, side ? 0.85f : 1.0f);
    switch (ic.kind) {
        case PC_HUD_MENU: DrawMenu(dl, c, g); break;
        case PC_HUD_SAVE: DrawSave(dl, c, g); break;
        case PC_HUD_PAD: DrawPad(dl, c, g); break;
        case PC_HUD_BAG: DrawBag(dl, c, g); break;
        case PC_HUD_MAP: DrawMap(dl, c, g); break;
        case PC_HUD_FURNITURE: DrawChair(dl, c, g); break;
        case PC_HUD_CLOSE: DrawClose(dl, c, g); break;
        case PC_HUD_FURN_DONE: DrawDone(dl, c, g); break;
        case PC_HUD_FURN_ROT_L: DrawRotate(dl, c, g, -1); break;
        case PC_HUD_FURN_ROT_R: DrawRotate(dl, c, g, 1); break;
        case PC_HUD_FURN_STORE: DrawStore(dl, c, g); break;
        case PC_HUD_TOOL:
        case PC_HUD_TOOL_PREV:
        case PC_HUD_TOOL_NEXT:
            DrawTool(dl, c, g, ic.glyph, ic.gold != 0);
            break;
        default: break;
    }
}

bool TouchHudWanted() {
    PCTouchHudIcon icons[4];
    return pc_touch_full_on() && pc_touch_full_hud(icons, 4) > 0;
}

void TouchHud() {
    static PCTouchHudIcon icons[64];
    int n = pc_touch_full_hud(icons, 64);
    if (n <= 0) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float U = W < H ? W : H;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    /* the furniture mode: the floor's grid and the picked piece */
    if (pc_touch_full_furniture_mode()) {
        float q[8];
        int ok = 1, ux = 0, uz = 0;

        if (pc_touch_full_furniture_floor(q, &ux, &uz) && ux > 0 && uz > 0) {
            for (int i = 0; i <= ux; i++) {
                float t = (float)i / ux;
                ImVec2 a((q[0] + (q[2] - q[0]) * t) * W, (q[1] + (q[3] - q[1]) * t) * H);
                ImVec2 b((q[6] + (q[4] - q[6]) * t) * W, (q[7] + (q[5] - q[7]) * t) * H);
                dl->AddLine(a, b, IM_COL32(0xFF, 0xFF, 0xFF, 0x50), 1.5f);
            }
            for (int i = 0; i <= uz; i++) {
                float t = (float)i / uz;
                ImVec2 a((q[0] + (q[6] - q[0]) * t) * W, (q[1] + (q[7] - q[1]) * t) * H);
                ImVec2 b((q[2] + (q[4] - q[2]) * t) * W, (q[3] + (q[5] - q[3]) * t) * H);
                dl->AddLine(a, b, IM_COL32(0xFF, 0xFF, 0xFF, 0x50), 1.5f);
            }
        }
        if (pc_touch_full_furniture_outline(q, &ok)) {
            ImVec2 pts[4] = { ImVec2(q[0] * W, q[1] * H), ImVec2(q[2] * W, q[3] * H), ImVec2(q[4] * W, q[5] * H),
                              ImVec2(q[6] * W, q[7] * H) };
            ImU32 fill = ok ? IM_COL32(0x60, 0xE0, 0x80, 0x50) : IM_COL32(0xF0, 0x50, 0x50, 0x60);
            ImU32 line = ok ? IM_COL32(0x60, 0xE0, 0x80, 0xFF) : IM_COL32(0xF0, 0x50, 0x50, 0xFF);
            dl->AddQuadFilled(pts[0], pts[1], pts[2], pts[3], fill);
            dl->AddQuad(pts[0], pts[1], pts[2], pts[3], line, 3.0f * g_scale);
        }
        const char* hint = pc_touch_full_furniture_hint(Korean() ? 1 : 0);
        if (hint != nullptr && hint[0] != '\0') {
            ImVec2 ts = ImGui::CalcTextSize(hint, nullptr, false, W * 0.6f);
            ImVec2 p((W - ts.x) * 0.5f, 0.17f * U);
            float pad = 8.0f * g_scale;
            dl->AddRectFilled(ImVec2(p.x - pad, p.y - pad * 0.6f), ImVec2(p.x + ts.x + pad, p.y + ts.y + pad * 0.6f),
                              IM_COL32(0xFF, 0xF8, 0xE8, 0xE0), 10.0f * g_scale);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), p, IM_COL32(0x4A, 0x3B, 0x2A, 0xFF), hint, nullptr,
                        W * 0.6f);
        }
    }

    /* the tool strip's band behind its three (when there is another to take) */
    bool sides = false;
    for (int i = 0; i < n; i++) {
        sides = sides || icons[i].kind == PC_HUD_TOOL_PREV || icons[i].kind == PC_HUD_TOOL_NEXT;
    }
    for (int i = 0; i < n && sides; i++) {
        if (icons[i].kind == PC_HUD_TOOL) {
            ImVec2 c(icons[i].x * W, icons[i].y * H);
            float r = icons[i].r * U;
            dl->AddRectFilled(ImVec2(c.x - r * 2.9f, c.y - r * 0.82f), ImVec2(c.x + r * 2.9f, c.y + r * 0.82f),
                              IM_COL32(0x30, 0x20, 0x10, 0x40), r);
            /* the arrows of a swipe */
            float ax = r * 3.25f, ah = r * 0.28f;
            dl->AddTriangleFilled(ImVec2(c.x - ax, c.y), ImVec2(c.x - ax + ah, c.y - ah), ImVec2(c.x - ax + ah, c.y + ah),
                                  IM_COL32(0xFF, 0xF8, 0xE8, 0xC0));
            dl->AddTriangleFilled(ImVec2(c.x + ax, c.y), ImVec2(c.x + ax - ah, c.y + ah), ImVec2(c.x + ax - ah, c.y - ah),
                                  IM_COL32(0xFF, 0xF8, 0xE8, 0xC0));
        }
    }
    for (int i = 0; i < n; i++) {
        DrawIcon(dl, icons[i], W, H, U);
    }
}

} // namespace gui
