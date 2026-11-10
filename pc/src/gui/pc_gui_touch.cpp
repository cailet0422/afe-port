/* pc_gui_touch.cpp - the full touch mode's icons (pc_touch_full.h), drawn
 * with ImGui's draw lists over the game. The pictures are drawn here from
 * shapes, as stickers (round parts in one dark outline): a satchel for the
 * pockets, a folded map, a floppy disk, an armchair, a gamepad, the tools; nothing
 * is taken from the game's own pictures. PC_TOUCH_ICON_SHEET=1 shows them
 * all on one sheet. */
#include "pc_gui_internal.h"

extern "C" {
#include "pc_touch_full.h"
#include "pc_platform.h"
}

#include <float.h>
#include <initializer_list>
#include <math.h>
#include <stdlib.h>

namespace gui {

/* The look: stickers. Every picture is made of round, chunky parts with one
 * dark brown outline around the whole (Pen: the parts are drawn twice, first
 * grown by the outline's width in its colour, then in their own), in warm
 * colours, on a cream button with a tan rim that sinks when pressed. */
static ImU32 Rgb(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}

static const ImU32 kInk = IM_COL32(0x4A, 0x34, 0x24, 0xFF);    /* outlines */
static const ImU32 kCream = IM_COL32(0xFF, 0xF9, 0xE6, 0xFF);
static const ImU32 kWood = IM_COL32(0xC2, 0x8A, 0x52, 0xFF);   /* handles */
static const ImU32 kWoodDk = IM_COL32(0x9A, 0x66, 0x38, 0xFF);
static const ImU32 kMetal = IM_COL32(0xC9, 0xD3, 0xDC, 0xFF);  /* blades */
static const ImU32 kGold = IM_COL32(0xF7, 0xCD, 0x46, 0xFF);
static const ImU32 kLeaf = IM_COL32(0x6C, 0xC2, 0x4A, 0xFF);
static const ImU32 kLeafDk = IM_COL32(0x3E, 0x8F, 0x3A, 0xFF);
static const ImU32 kRed = IM_COL32(0xE8, 0x60, 0x4C, 0xFF);
static const ImU32 kBlue = IM_COL32(0x5A, 0xA9, 0xE6, 0xFF);
static const ImU32 kShine = IM_COL32(0xFF, 0xFF, 0xFF, 0xA0);

/* Draws a picture's parts about c, in units of r. Used as
 *   for (Pen p(dl, c, r); p.next();) { p.box(...); p.ball(...); if (p.detail()) ... }
 * the first round lays the outline under everything, the second the parts. */
struct Pen {
    ImDrawList* dl;
    ImVec2 c;
    float r;
    float o; /* the outline's width */
    int pass;

    Pen(ImDrawList* dl_, ImVec2 c_, float r_) : dl(dl_), c(c_), r(r_), o(r_ * 0.085f), pass(-1) {}
    bool next() { return ++pass < 2; }
    bool detail() const { return pass == 1; }
    ImVec2 at(float x, float y) const { return ImVec2(c.x + x * r, c.y + y * r); }
    ImU32 col(ImU32 fill) const { return pass == 0 ? kInk : fill; }
    float grow() const { return pass == 0 ? o : 0.0f; }

    void ball(float x, float y, float rad, ImU32 fill) { dl->AddCircleFilled(at(x, y), rad * r + grow(), col(fill)); }
    void oval(float x, float y, float rx, float ry, ImU32 fill) {
        dl->AddEllipseFilled(at(x, y), ImVec2(rx * r + grow(), ry * r + grow()), col(fill));
    }
    /* a line with round ends */
    void cap(float x0, float y0, float x1, float y1, float w, ImU32 fill) {
        float t = w * r + 2.0f * grow();
        dl->AddLine(at(x0, y0), at(x1, y1), col(fill), t);
        dl->AddCircleFilled(at(x0, y0), t * 0.5f, col(fill));
        dl->AddCircleFilled(at(x1, y1), t * 0.5f, col(fill));
    }
    void box(float x0, float y0, float x1, float y1, float round, ImU32 fill, ImDrawFlags flags = 0) {
        float g = grow();
        ImVec2 a = at(x0, y0), b = at(x1, y1);
        dl->AddRectFilled(ImVec2(a.x - g, a.y - g), ImVec2(b.x + g, b.y + g), col(fill), round * r + g, flags);
    }
    /* a convex shape */
    void poly(std::initializer_list<ImVec2> unit, ImU32 fill) {
        ImVec2 pts[16];
        int n = 0;
        for (const ImVec2& u : unit) {
            if (n < 16) {
                pts[n++] = at(u.x, u.y);
            }
        }
        dl->AddConvexPolyFilled(pts, n, col(fill));
        if (pass == 0) {
            dl->AddPolyline(pts, n, kInk, ImDrawFlags_Closed, o * 2.0f);
            for (int i = 0; i < n; i++) {
                dl->AddCircleFilled(pts[i], o, kInk);
            }
        }
    }
    void arc(float x, float y, float rad, float a0, float a1, float w, ImU32 fill) {
        float t = w * r + 2.0f * grow();
        dl->PathArcTo(at(x, y), rad * r, a0, a1, 24);
        dl->PathStroke(col(fill), 0, t);
        dl->AddCircleFilled(at(x + cosf(a0) * rad, y + sinf(a0) * rad), t * 0.5f, col(fill));
        dl->AddCircleFilled(at(x + cosf(a1) * rad, y + sinf(a1) * rad), t * 0.5f, col(fill));
    }
    void curve(ImVec2 a, ImVec2 b, ImVec2 c2, float w, ImU32 fill) {
        float t = w * r + 2.0f * grow();
        dl->AddBezierQuadratic(at(a.x, a.y), at(b.x, b.y), at(c2.x, c2.y), col(fill), t);
        dl->AddCircleFilled(at(a.x, a.y), t * 0.5f, col(fill));
        dl->AddCircleFilled(at(c2.x, c2.y), t * 0.5f, col(fill));
    }
    /* a leaf lying along the turn a (radians), its middle at x, y */
    void leaf(float x, float y, float len, float a, ImU32 fill) {
        float ca = cosf(a), sa = sinf(a);
        ImVec2 pts[14];
        int n = 0;
        for (int i = 0; i <= 6; i++) { /* one side, then the other: a lens */
            float t = (float)i / 6.0f;
            float u = (t - 0.5f) * len, v = sinf(t * 3.14159265f) * len * 0.30f;
            pts[n++] = at(x + u * ca - v * sa, y + u * sa + v * ca);
        }
        for (int i = 5; i >= 1; i--) {
            float t = (float)i / 6.0f;
            float u = (t - 0.5f) * len, v = -sinf(t * 3.14159265f) * len * 0.30f;
            pts[n++] = at(x + u * ca - v * sa, y + u * sa + v * ca);
        }
        dl->AddConvexPolyFilled(pts, n, col(fill));
        if (pass == 0) {
            dl->AddPolyline(pts, n, kInk, ImDrawFlags_Closed, o * 2.0f);
        } else {
            dl->AddLine(at(x - len * 0.32f * ca, y - len * 0.32f * sa), at(x + len * 0.36f * ca, y + len * 0.36f * sa),
                        kLeafDk, r * 0.05f); /* its vein */
        }
    }
    /* over the parts, no outline (second round only) */
    void line(float x0, float y0, float x1, float y1, float w, ImU32 colour) {
        if (pass == 1) {
            dl->AddLine(at(x0, y0), at(x1, y1), colour, w * r);
        }
    }
    /* a round window in a part (a hoop's inside), with its own outline */
    void glass(float x, float y, float rad, ImU32 colour) {
        if (pass == 1) {
            dl->AddCircleFilled(at(x, y), rad * r + o * 0.8f, kInk);
            dl->AddCircleFilled(at(x, y), rad * r, colour);
        }
    }
    void dot(float x, float y, float rad, ImU32 colour) {
        if (pass == 1) {
            dl->AddCircleFilled(at(x, y), rad * r, colour);
        }
    }
};

static const float PI = 3.14159265f;

/* --- Pictures (c the centre, r the room for it) ------------------------- */

/* A list: three leaves' worth of lines */
static void DrawMenu(ImDrawList* dl, ImVec2 c, float r) {
    for (Pen p(dl, c, r); p.next();) {
        for (int i = -1; i <= 1; i++) {
            p.ball(-0.52f, 0.46f * i, 0.11f, kLeaf);
            p.cap(-0.16f, 0.46f * i, 0.56f, 0.46f * i, 0.17f, kWood);
        }
    }
}

/* A blue floppy disk (saving) */
static void DrawSave(ImDrawList* dl, ImVec2 c, float r) {
    for (Pen p(dl, c, r); p.next();) {
        p.poly({ { -0.66f, -0.66f }, { 0.44f, -0.66f }, { 0.66f, -0.44f }, { 0.66f, 0.66f }, { -0.66f, 0.66f } }, kBlue);
        p.box(-0.34f, -0.66f, 0.30f, -0.22f, 0.04f, kMetal);  /* its shutter */
        p.line(0.12f, -0.56f, 0.12f, -0.32f, 0.11f, Rgb(0x6C, 0x7A, 0x88));
        p.box(-0.42f, 0.08f, 0.42f, 0.66f, 0.06f, kCream);    /* its label */
        p.line(-0.26f, 0.28f, 0.26f, 0.28f, 0.05f, Rgb(0xC9, 0xB8, 0x94));
        p.line(-0.26f, 0.46f, 0.26f, 0.46f, 0.05f, Rgb(0xC9, 0xB8, 0x94));
    }
}

static void DrawPad(ImDrawList* dl, ImVec2 c, float r) {
    ImU32 body = Rgb(0x7A, 0x6C, 0xD6);
    for (Pen p(dl, c, r); p.next();) {
        p.ball(-0.50f, 0.20f, 0.34f, body); /* its grips */
        p.ball(0.50f, 0.20f, 0.34f, body);
        p.box(-0.80f, -0.44f, 0.80f, 0.26f, 0.30f, body);
        p.line(-0.60f, -0.10f, -0.26f, -0.10f, 0.13f, kCream); /* a cross */
        p.line(-0.43f, -0.27f, -0.43f, 0.07f, 0.13f, kCream);
        p.dot(0.34f, 0.00f, 0.12f, kLeaf);
        p.dot(0.56f, -0.18f, 0.11f, kRed);
        p.dot(0.14f, -0.20f, 0.08f, kGold);
        p.line(-0.50f, -0.36f, 0.10f, -0.36f, 0.05f, Rgb(0xFF, 0xFF, 0xFF, 0x60));
    }
}

/* A satchel with a leaf for its clasp (the pockets) */
static void DrawBag(ImDrawList* dl, ImVec2 c, float r) {
    ImU32 leather = Rgb(0xE0, 0xA1, 0x58);
    ImU32 flap = Rgb(0xB9, 0x74, 0x38);
    for (Pen p(dl, c, r); p.next();) {
        p.arc(0.0f, -0.30f, 0.40f, PI * 1.05f, PI * 1.95f, 0.14f, flap); /* its strap */
        p.box(-0.72f, -0.32f, 0.72f, 0.70f, 0.26f, leather);
        p.box(-0.72f, -0.32f, 0.72f, 0.12f, 0.24f, flap);
        p.line(-0.50f, 0.52f, 0.50f, 0.52f, 0.05f, Rgb(0xFF, 0xFF, 0xFF, 0x50));
        p.ball(0.0f, 0.14f, 0.19f, kCream);
        p.leaf(0.0f, 0.14f, 0.30f, -PI * 0.25f, kLeaf);
    }
}

/* A folded map with a river and a pin */
static void DrawMap(ImDrawList* dl, ImVec2 c, float r) {
    for (Pen p(dl, c, r); p.next();) {
        p.poly({ { -0.74f, -0.44f }, { -0.25f, -0.62f }, { -0.25f, 0.46f }, { -0.74f, 0.64f } }, Rgb(0xB4, 0xE2, 0x8E));
        p.poly({ { -0.25f, -0.62f }, { 0.25f, -0.44f }, { 0.25f, 0.64f }, { -0.25f, 0.46f } }, Rgb(0x92, 0xCF, 0x78));
        p.poly({ { 0.25f, -0.44f }, { 0.74f, -0.62f }, { 0.74f, 0.46f }, { 0.25f, 0.64f } }, Rgb(0xB4, 0xE2, 0x8E));
        if (p.detail()) {
            dl->AddBezierCubic(p.at(-0.70f, 0.30f), p.at(-0.30f, -0.05f), p.at(0.10f, 0.55f), p.at(0.70f, 0.12f), kBlue,
                               r * 0.13f);
        }
        p.poly({ { 0.02f, -0.30f }, { 0.38f, -0.30f }, { 0.20f, 0.08f } }, kRed); /* the pin */
        p.ball(0.20f, -0.36f, 0.21f, kRed);
        p.dot(0.20f, -0.36f, 0.08f, kCream);
    }
}

static void DrawChair(ImDrawList* dl, ImVec2 c, float r, ImU32 cloth = IM_COL32(0xF0, 0x8E, 0x7A, 0xFF)) {
    ImU32 arm = Rgb(0xD4, 0x66, 0x58);
    for (Pen p(dl, c, r); p.next();) {
        p.cap(-0.46f, 0.42f, -0.50f, 0.66f, 0.13f, kWoodDk); /* legs */
        p.cap(0.46f, 0.42f, 0.50f, 0.66f, 0.13f, kWoodDk);
        p.box(-0.48f, -0.68f, 0.48f, 0.20f, 0.26f, cloth);   /* back */
        p.box(-0.50f, 0.02f, 0.50f, 0.42f, 0.12f, Rgb(0xFB, 0xB6, 0xA2)); /* seat */
        p.box(-0.78f, -0.18f, -0.46f, 0.46f, 0.15f, arm);    /* arms */
        p.box(0.46f, -0.18f, 0.78f, 0.46f, 0.15f, arm);
        p.dot(-0.16f, -0.30f, 0.05f, arm);
        p.dot(0.16f, -0.30f, 0.05f, arm);
        p.line(-0.26f, -0.54f, 0.10f, -0.54f, 0.05f, Rgb(0xFF, 0xFF, 0xFF, 0x70));
    }
}

static void DrawClose(ImDrawList* dl, ImVec2 c, float r) {
    for (Pen p(dl, c, r); p.next();) {
        p.cap(-0.42f, -0.42f, 0.42f, 0.42f, 0.24f, kRed);
        p.cap(0.42f, -0.42f, -0.42f, 0.42f, 0.24f, kRed);
    }
}

static void DrawDone(ImDrawList* dl, ImVec2 c, float r) {
    for (Pen p(dl, c, r); p.next();) {
        p.cap(-0.50f, 0.04f, -0.14f, 0.40f, 0.24f, kLeaf);
        p.cap(-0.14f, 0.40f, 0.52f, -0.36f, 0.24f, kLeaf);
    }
}

/* A turning arrow: dir 1 turns right (clockwise) */
static void DrawRotate(ImDrawList* dl, ImVec2 c, float r, int dir) {
    float a0 = dir > 0 ? PI * 1.15f : PI * 1.85f;
    float a1 = dir > 0 ? PI * 2.45f : PI * 0.55f;
    float tx = cosf(a1) * 0.50f, ty = sinf(a1) * 0.50f;
    float ta = a1 + (dir > 0 ? PI * 0.5f : -PI * 0.5f);
    ImVec2 fwd(cosf(ta) * 0.34f, sinf(ta) * 0.34f), side(cosf(a1) * 0.30f, sinf(a1) * 0.30f);

    for (Pen p(dl, c, r); p.next();) {
        p.arc(0.0f, 0.0f, 0.50f, a0 < a1 ? a0 : a1, a0 < a1 ? a1 : a0, 0.20f, kBlue);
        p.poly({ { tx + fwd.x, ty + fwd.y }, { tx + side.x, ty + side.y }, { tx - side.x, ty - side.y } }, kBlue);
    }
}

/* An arrow pointing down, for what goes in */
static void ArrowDown(Pen& p, float x, float y0, float y1, ImU32 fill) {
    p.cap(x, y0, x, y1 - 0.16f, 0.17f, fill);
    p.poly({ { x - 0.26f, y1 - 0.26f }, { x + 0.26f, y1 - 0.26f }, { x, y1 + 0.04f } }, fill);
}

/* The satchel, smaller, with an arrow going in */
static void DrawStore(ImDrawList* dl, ImVec2 c, float r) {
    DrawBag(dl, ImVec2(c.x, c.y + r * 0.26f), r * 0.68f);
    for (Pen p(dl, c, r); p.next();) {
        ArrowDown(p, 0.0f, -0.84f, -0.26f, kLeaf);
    }
}

/* A camera under a turning arrow (the room's angle) */
static void DrawCamAngle(ImDrawList* dl, ImVec2 c, float r) {
    ImU32 body = Rgb(0x8C, 0xA0, 0xB8);
    float a1 = PI * 1.86f;
    float tx = cosf(a1) * 0.74f, ty = 0.22f + sinf(a1) * 0.74f;

    for (Pen p(dl, c, r); p.next();) {
        p.arc(0.0f, 0.22f, 0.74f, PI * 1.14f, a1, 0.14f, Rgb(0xF2, 0x9A, 0x38));
        p.poly({ { tx + 0.20f, ty + 0.26f }, { tx - 0.26f, ty + 0.02f }, { tx + 0.24f, ty - 0.20f } },
               Rgb(0xF2, 0x9A, 0x38));
        p.box(-0.22f, -0.20f, 0.10f, 0.0f, 0.06f, body); /* its top */
        p.box(-0.52f, -0.08f, 0.52f, 0.62f, 0.16f, body);
        p.ball(0.0f, 0.27f, 0.25f, Rgb(0xE8, 0xF4, 0xFF));
        p.dot(0.0f, 0.27f, 0.13f, Rgb(0x4A, 0x5C, 0x78));
        p.dot(-0.05f, 0.22f, 0.05f, kShine);
        p.dot(0.36f, 0.06f, 0.06f, kRed);
    }
}

/* A magnifier (the room's camera nearer or farther) */
static void DrawZoom(ImDrawList* dl, ImVec2 c, float r) {
    for (Pen p(dl, c, r); p.next();) {
        p.cap(0.24f, 0.24f, 0.60f, 0.60f, 0.22f, kWood);
        p.ball(-0.12f, -0.12f, 0.50f, kGold);
        p.glass(-0.12f, -0.12f, 0.33f, Rgb(0xDC, 0xF2, 0xFF));
        p.line(-0.32f, -0.12f, 0.08f, -0.12f, 0.10f, kInk);
        p.line(-0.12f, -0.32f, -0.12f, 0.08f, 0.10f, kInk);
        p.dot(-0.30f, -0.30f, 0.06f, kShine);
    }
}

/* A light bulb (the house's light switch) */
static void DrawLight(ImDrawList* dl, ImVec2 c, float r) {
    for (Pen p(dl, c, r); p.next();) {
        p.box(-0.20f, 0.20f, 0.20f, 0.62f, 0.10f, kMetal);
        p.ball(0.0f, -0.16f, 0.48f, kGold);
        p.line(-0.20f, 0.36f, 0.20f, 0.36f, 0.05f, Rgb(0x88, 0x94, 0xA0));
        p.line(-0.20f, 0.48f, 0.20f, 0.48f, 0.05f, Rgb(0x88, 0x94, 0xA0));
        if (p.detail()) {
            dl->PathArcTo(p.at(0.0f, -0.16f), r * 0.30f, PI * 1.10f, PI * 1.50f, 10);
            dl->PathStroke(kShine, 0, r * 0.08f);
        }
    }
}

/* A door and an arrow out of it (leaves the NES game) */
static void DrawExit(ImDrawList* dl, ImVec2 c, float r) {
    for (Pen p(dl, c, r); p.next();) {
        p.box(-0.62f, -0.66f, 0.06f, 0.66f, 0.10f, kWood);
        if (p.detail()) {
            dl->AddRect(p.at(-0.50f, -0.52f), p.at(-0.06f, 0.52f), kWoodDk, r * 0.06f, 0, r * 0.05f);
        }
        p.dot(-0.10f, 0.06f, 0.07f, kGold);
        p.cap(0.02f, 0.0f, 0.44f, 0.0f, 0.18f, kRed);
        p.poly({ { 0.74f, 0.0f }, { 0.40f, -0.28f }, { 0.40f, 0.28f } }, kRed);
    }
}

/* A menu's key: an arrow, a play mark, a backspace or its label */
static void DrawKey(ImDrawList* dl, ImVec2 c, float r, int glyph, const char* label) {
    ImU32 arrow = Rgb(0xF2, 0x9A, 0x38);

    if (glyph == PC_KEYG_TEXT) {
        if (label != nullptr) {
            float size = ImGui::GetFontSize();
            ImVec2 ts = ImGui::CalcTextSize(label);
            float s = ts.x > r * 1.5f ? r * 1.5f / ts.x : 1.0f;
            ImVec2 pos(c.x - ts.x * s * 0.5f, c.y - ts.y * s * 0.5f);
            dl->AddText(ImGui::GetFont(), size * s, pos, kInk, label);
            dl->AddText(ImGui::GetFont(), size * s, ImVec2(pos.x + size * s * 0.04f, pos.y), kInk, label);
        }
        return;
    }
    for (Pen p(dl, c, r); p.next();) {
        switch (glyph) {
            case PC_KEYG_UP: p.poly({ { 0.0f, -0.46f }, { 0.48f, 0.32f }, { -0.48f, 0.32f } }, arrow); break;
            case PC_KEYG_DOWN: p.poly({ { 0.0f, 0.46f }, { -0.48f, -0.32f }, { 0.48f, -0.32f } }, arrow); break;
            case PC_KEYG_LEFT: p.poly({ { -0.46f, 0.0f }, { 0.32f, -0.48f }, { 0.32f, 0.48f } }, arrow); break;
            case PC_KEYG_RIGHT: p.poly({ { 0.46f, 0.0f }, { -0.32f, 0.48f }, { -0.32f, -0.48f } }, arrow); break;
            case PC_KEYG_PLAY: p.poly({ { 0.54f, 0.0f }, { -0.34f, 0.52f }, { -0.34f, -0.52f } }, kLeaf); break;
            case PC_KEYG_BACK:
                p.poly({ { -0.66f, 0.0f }, { -0.26f, -0.40f }, { 0.58f, -0.40f }, { 0.58f, 0.40f }, { -0.26f, 0.40f } },
                       Rgb(0xF6, 0xD9, 0x9C));
                p.line(-0.04f, -0.17f, 0.30f, 0.17f, 0.11f, kInk);
                p.line(-0.04f, 0.17f, 0.30f, -0.17f, 0.11f, kInk);
                break;
            default: break;
        }
    }
}

/* The tools: a handle and what is on it */
static void DrawTool(ImDrawList* dl, ImVec2 c, float r, int glyph, bool gold) {
    ImU32 metal = gold ? kGold : kMetal;
    ImU32 handle = gold ? Rgb(0xD9, 0xA4, 0x2C) : kWood;

    if (glyph == PC_GLYPH_FURNITURE) {
        DrawChair(dl, c, r * 0.9f);
        return;
    }
    for (Pen p(dl, c, r); p.next();) {
        switch (glyph) {
            case PC_GLYPH_NET:
                p.cap(-0.62f, 0.62f, 0.00f, 0.00f, 0.14f, handle);
                p.ball(0.24f, -0.24f, 0.46f, metal); /* the hoop */
                p.glass(0.24f, -0.24f, 0.31f, Rgb(0xF4, 0xFB, 0xFF));
                for (int i = -1; i <= 1; i++) {
                    p.line(0.00f, -0.24f + 0.14f * i, 0.48f, -0.24f + 0.14f * i, 0.03f, Rgb(0xA8, 0xB8, 0xC4));
                    p.line(0.24f + 0.14f * i, -0.48f, 0.24f + 0.14f * i, 0.00f, 0.03f, Rgb(0xA8, 0xB8, 0xC4));
                }
                break;
            case PC_GLYPH_AXE:
                p.cap(-0.50f, 0.66f, 0.18f, -0.56f, 0.16f, handle);
                p.poly({ { 0.02f, -0.56f }, { 0.56f, -0.78f }, { 0.74f, -0.38f }, { 0.66f, 0.00f }, { 0.14f, -0.24f } },
                       metal);
                p.line(0.58f, -0.66f, 0.66f, -0.14f, 0.06f, kShine);
                break;
            case PC_GLYPH_SHOVEL:
                p.cap(0.50f, -0.62f, -0.10f, 0.10f, 0.14f, handle);
                p.cap(0.34f, -0.72f, 0.66f, -0.50f, 0.15f, handle);
                p.poly({ { -0.14f, -0.08f }, { 0.12f, 0.20f }, { -0.14f, 0.62f }, { -0.44f, 0.74f }, { -0.66f, 0.50f },
                         { -0.56f, 0.18f } },
                       metal);
                p.line(-0.20f, 0.10f, -0.46f, 0.50f, 0.06f, kShine);
                break;
            case PC_GLYPH_ROD:
                p.curve({ -0.66f, 0.66f }, { 0.06f, -0.60f }, { 0.60f, -0.52f }, 0.13f, handle);
                p.line(0.60f, -0.52f, 0.52f, 0.24f, 0.035f, kInk);
                p.ball(-0.40f, 0.36f, 0.13f, metal); /* its reel */
                p.ball(0.52f, 0.38f, 0.17f, kRed);   /* its float */
                if (p.detail()) {
                    dl->PathArcTo(p.at(0.52f, 0.38f), r * 0.17f, PI, PI * 2.0f, 10);
                    dl->PathFillConvex(kCream);
                }
                break;
            case PC_GLYPH_UMBRELLA:
                p.cap(0.0f, -0.20f, 0.0f, 0.50f, 0.11f, handle);
                p.arc(-0.14f, 0.50f, 0.14f, 0.0f, PI, 0.11f, handle);
                p.poly({ { -0.70f, -0.06f }, { -0.62f, -0.36f }, { -0.38f, -0.62f }, { 0.0f, -0.74f }, { 0.38f, -0.62f },
                         { 0.62f, -0.36f }, { 0.70f, -0.06f } },
                       kRed);
                p.poly({ { -0.24f, -0.06f }, { -0.20f, -0.50f }, { 0.0f, -0.74f }, { 0.20f, -0.50f }, { 0.24f, -0.06f } },
                       kCream);
                break;
            case PC_GLYPH_BALLOON:
                if (p.detail()) {
                    dl->AddBezierCubic(p.at(0.0f, 0.34f), p.at(-0.22f, 0.48f), p.at(0.22f, 0.58f), p.at(0.0f, 0.78f), kInk,
                                       r * 0.05f);
                }
                p.poly({ { -0.12f, 0.40f }, { 0.12f, 0.40f }, { 0.0f, 0.22f } }, kRed);
                p.oval(0.0f, -0.22f, 0.46f, 0.54f, kRed);
                p.dot(-0.18f, -0.42f, 0.09f, kShine);
                break;
            case PC_GLYPH_PINWHEEL: {
                ImU32 cols[4] = { kRed, kGold, kBlue, kLeaf };
                p.cap(0.0f, -0.10f, 0.0f, 0.72f, 0.11f, handle);
                for (int i = 0; i < 4; i++) {
                    float a = PI * 0.5f * i + PI * 0.25f;
                    float ox = cosf(a) * 0.52f, oy = sinf(a) * 0.52f;
                    float sx = cosf(a + PI * 0.5f) * 0.32f, sy = sinf(a + PI * 0.5f) * 0.32f;
                    p.poly({ { 0.0f, -0.18f }, { ox, -0.18f + oy }, { ox + sx, -0.18f + oy + sy } }, cols[i]);
                }
                p.ball(0.0f, -0.18f, 0.10f, kCream);
                break;
            }
            case PC_GLYPH_FAN: /* a round paper fan */
                p.cap(0.02f, 0.10f, -0.42f, 0.66f, 0.13f, handle);
                p.ball(0.14f, -0.20f, 0.50f, Rgb(0x8E, 0xD6, 0xC0));
                p.line(0.02f, 0.10f, 0.14f, -0.66f, 0.035f, Rgb(0x58, 0xA8, 0x94));
                p.line(0.02f, 0.10f, -0.22f, -0.46f, 0.035f, Rgb(0x58, 0xA8, 0x94));
                p.line(0.02f, 0.10f, 0.50f, -0.46f, 0.035f, Rgb(0x58, 0xA8, 0x94));
                p.dot(0.14f, -0.24f, 0.13f, kRed);
                break;
            case PC_GLYPH_FLOWER:
                p.cap(0.0f, 0.0f, 0.0f, 0.72f, 0.10f, kLeafDk);
                p.leaf(0.20f, 0.44f, 0.40f, -PI * 0.20f, kLeaf);
                for (int i = 0; i < 5; i++) {
                    float a = PI * 2.0f * i / 5.0f - PI * 0.5f;
                    p.ball(cosf(a) * 0.28f, -0.20f + sinf(a) * 0.28f, 0.21f, Rgb(0xFF, 0x9E, 0xC2));
                }
                p.dot(0.0f, -0.20f, 0.15f, kGold);
                break;
            case PC_GLYPH_POPPER:
                p.poly({ { -0.60f, 0.66f }, { -0.06f, -0.18f }, { 0.26f, 0.14f } }, kGold);
                p.line(-0.30f, 0.20f, -0.02f, 0.44f, 0.08f, kRed);
                p.line(-0.14f, -0.04f, 0.16f, 0.24f, 0.08f, kRed);
                p.ball(0.34f, -0.42f, 0.10f, kRed);
                p.ball(0.60f, -0.10f, 0.09f, kBlue);
                p.ball(0.08f, -0.62f, 0.09f, kLeaf);
                p.cap(0.50f, -0.62f, 0.62f, -0.50f, 0.07f, Rgb(0xFF, 0x9E, 0xC2));
                break;
            case PC_GLYPH_NONE: { /* an open hand, as a mitten */
                ImU32 skin = Rgb(0xFF, 0xD9, 0xAE);
                p.cap(-0.30f, 0.04f, -0.34f, -0.36f, 0.19f, skin);
                p.cap(-0.10f, 0.00f, -0.12f, -0.54f, 0.19f, skin);
                p.cap(0.10f, 0.00f, 0.12f, -0.50f, 0.19f, skin);
                p.cap(0.30f, 0.04f, 0.34f, -0.30f, 0.19f, skin);
                p.cap(0.34f, 0.30f, 0.62f, 0.06f, 0.19f, skin);
                p.box(-0.40f, -0.06f, 0.40f, 0.62f, 0.24f, skin);
                p.line(-0.18f, 0.36f, 0.18f, 0.36f, 0.05f, Rgb(0xE8, 0xB0, 0x80));
                break;
            }
            default: /* something else in hand: a present */
                p.box(-0.50f, -0.10f, 0.50f, 0.62f, 0.10f, Rgb(0xF6, 0xD9, 0x9C));
                p.box(-0.58f, -0.30f, 0.58f, 0.02f, 0.10f, Rgb(0xFB, 0xE8, 0xBE));
                p.line(0.0f, -0.30f, 0.0f, 0.62f, 0.14f, kRed);
                p.ball(-0.18f, -0.46f, 0.16f, kRed);
                p.ball(0.18f, -0.46f, 0.16f, kRed);
                break;
        }
    }
}

/* --- The icons ------------------------------------------------------------ */

static ImU32 Fade(ImU32 colour, float alpha) {
    int a = (int)(((colour >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
    return (colour & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
}

/* The button under a picture: a cream face in a tan rim (green when it is
 * on), outlined, over its shadow; pressed, it sinks onto the shadow. Returns
 * how far it sank. */
static float Button(ImDrawList* dl, ImVec2 c, float r, bool pressed, bool active, float alpha, float round) {
    float sink = pressed ? r * 0.09f : 0.0f;
    ImU32 rim = active ? Rgb(0x8C, 0xD8, 0x6E) : Rgb(0xF3, 0xCE, 0x8A);
    ImU32 face = pressed ? Rgb(0xF6, 0xE6, 0xC0) : kCream;
    ImVec2 cc(c.x, c.y + sink);
    auto shape = [&](ImVec2 at, float rad, ImU32 colour) {
        if (round >= 1.0f) {
            dl->AddCircleFilled(at, rad, colour);
        } else {
            dl->AddRectFilled(ImVec2(at.x - rad, at.y - rad), ImVec2(at.x + rad, at.y + rad), colour, rad * round);
        }
    };

    shape(ImVec2(c.x, c.y + r * 0.13f), r, Fade(Rgb(0x3A, 0x24, 0x14, 0x66), alpha));
    shape(cc, r, Fade(kInk, alpha));
    shape(cc, r * 0.93f, Fade(rim, alpha));
    shape(cc, r * 0.80f, Fade(face, alpha));
    if (round >= 1.0f && !pressed) { /* a little light on its top left */
        dl->PathArcTo(cc, r * 0.66f, PI * 1.12f, PI * 1.42f, 10);
        dl->PathStroke(Fade(Rgb(0xFF, 0xFF, 0xFF, 0xD0), alpha), 0, r * 0.07f);
    }
    return sink;
}

static void DrawPicture(ImDrawList* dl, const PCTouchHudIcon& ic, ImVec2 c, float g) {
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
        case PC_HUD_CAM_ANGLE: DrawCamAngle(dl, c, g); break;
        case PC_HUD_CAM_ZOOM: DrawZoom(dl, c, g); break;
        case PC_HUD_LIGHT: DrawLight(dl, c, g); break;
        case PC_HUD_NES_EXIT: DrawExit(dl, c, g); break;
        case PC_HUD_DONE: DrawDone(dl, c, g); break;
        case PC_HUD_KEY: DrawKey(dl, c, g, ic.glyph, ic.label); break;
        case PC_HUD_FURN_SLOT: DrawChair(dl, c, g * 0.9f); break;
        case PC_HUD_TOOL:
        case PC_HUD_TOOL_PREV:
        case PC_HUD_TOOL_NEXT:
            DrawTool(dl, c, g, ic.glyph, ic.gold != 0);
            break;
        default: break;
    }
}

static void DrawIcon(ImDrawList* dl, const PCTouchHudIcon& ic, float W, float H, float U) {
    ImVec2 c(ic.x * W, ic.y * H);
    float r = ic.r * U;
    bool side = ic.kind == PC_HUD_TOOL_PREV || ic.kind == PC_HUD_TOOL_NEXT;
    bool slot = ic.kind == PC_HUD_FURN_SLOT;
    float sink = Button(dl, c, r, ic.pressed != 0, ic.active != 0, side ? 0.90f : 1.0f, slot ? 0.34f : 1.0f);

    DrawPicture(dl, ic, ImVec2(c.x, c.y + sink), r * 0.66f); /* the picture inside the face */
}

/* Every icon on one sheet (PC_TOUCH_ICON_SHEET=1: for drawing them) */
static void IconSheet(ImDrawList* dl, float W, float H) {
    struct Item { int kind, glyph, gold, pressed, active; };
    static const Item items[] = {
        { PC_HUD_MENU }, { PC_HUD_SAVE }, { PC_HUD_PAD }, { PC_HUD_PAD, 0, 0, 0, 1 }, { PC_HUD_BAG }, { PC_HUD_MAP },
        { PC_HUD_FURNITURE }, { PC_HUD_CLOSE }, { PC_HUD_DONE }, { PC_HUD_FURN_ROT_L }, { PC_HUD_FURN_ROT_R },
        { PC_HUD_FURN_STORE }, { PC_HUD_FURN_SLOT }, { PC_HUD_FURN_SLOT, 0, 0, 0, 1 }, { PC_HUD_CAM_ANGLE },
        { PC_HUD_CAM_ZOOM }, { PC_HUD_LIGHT }, { PC_HUD_NES_EXIT }, { PC_HUD_KEY, PC_KEYG_UP }, { PC_HUD_KEY, PC_KEYG_DOWN },
        { PC_HUD_KEY, PC_KEYG_LEFT }, { PC_HUD_KEY, PC_KEYG_RIGHT }, { PC_HUD_KEY, PC_KEYG_PLAY },
        { PC_HUD_KEY, PC_KEYG_BACK }, { PC_HUD_KEY, PC_KEYG_TEXT }, { PC_HUD_BAG, 0, 0, 1, 0 },
        { PC_HUD_TOOL, PC_GLYPH_NONE }, { PC_HUD_TOOL, PC_GLYPH_NET }, { PC_HUD_TOOL, PC_GLYPH_AXE },
        { PC_HUD_TOOL, PC_GLYPH_SHOVEL }, { PC_HUD_TOOL, PC_GLYPH_ROD }, { PC_HUD_TOOL, PC_GLYPH_UMBRELLA },
        { PC_HUD_TOOL, PC_GLYPH_BALLOON }, { PC_HUD_TOOL, PC_GLYPH_PINWHEEL }, { PC_HUD_TOOL, PC_GLYPH_FAN },
        { PC_HUD_TOOL, PC_GLYPH_FLOWER }, { PC_HUD_TOOL, PC_GLYPH_POPPER }, { PC_HUD_TOOL, PC_GLYPH_OTHER },
        { PC_HUD_TOOL, PC_GLYPH_NET, 1 }, { PC_HUD_TOOL, PC_GLYPH_AXE, 1 }, { PC_HUD_TOOL, PC_GLYPH_SHOVEL, 1 },
        { PC_HUD_TOOL, PC_GLYPH_ROD, 1 },
    };
    int n = (int)(sizeof(items) / sizeof(items[0]));
    int cols = W > H ? 9 : 6;
    int rows = (n + cols - 1) / cols;
    float cell = W / cols < H / rows ? W / cols : H / rows;
    float U = W < H ? W : H;

    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), Rgb(0x7D, 0xB8, 0x6A));
    for (int i = 0; i < n; i++) {
        PCTouchHudIcon ic = {};
        ic.kind = items[i].kind;
        ic.glyph = items[i].glyph;
        ic.gold = items[i].gold;
        ic.pressed = items[i].pressed;
        ic.active = items[i].active;
        ic.label = "OK";
        ic.x = (cell * (i % cols + 0.5f)) / W;
        ic.y = (cell * (i / cols + 0.5f)) / H;
        ic.r = cell * 0.40f / U;
        DrawIcon(dl, ic, W, H, U);
    }
}

/* The portrait layout's choices (pc_portrait.c): big cream buttons with an
 * orange rim at the bottom, from the bottom up as the game lists them */
static char s_choice_text[8][PC_PORTRAIT_CHOICE_LEN];

static void DrawPortraitChoices(ImDrawList* dl, int n, int cursor, float W, float H) {
    ImFont* font = ImGui::GetFont();

    for (int i = 0; i < n; i++) {
        float x0, y0, x1, y1;
        pc_portrait_button_rect(i, n, &x0, &y0, &x1, &y1);
        x0 *= W;
        x1 *= W;
        y0 *= H;
        y1 *= H;
        float bh = y1 - y0, r = bh * 0.5f, rim = bh * 0.06f;
        bool on = i == cursor;
        float ol = bh * 0.035f; /* the outline, as the icons' */
        ImU32 edge = on ? IM_COL32(0xF2, 0x9A, 0x38, 0xFF) : IM_COL32(0xF3, 0xCE, 0x8A, 0xFF);
        ImU32 fill = on ? IM_COL32(0xFF, 0xF0, 0xD2, 0xFF) : kCream;

        dl->AddRectFilled(ImVec2(x0, y0 + rim * 1.6f), ImVec2(x1, y1 + rim * 1.6f), Rgb(0x3A, 0x24, 0x14, 0x66), r);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kInk, r);
        dl->AddRectFilled(ImVec2(x0 + ol, y0 + ol), ImVec2(x1 - ol, y1 - ol), edge, r - ol);
        dl->AddRectFilled(ImVec2(x0 + ol + rim, y0 + ol + rim), ImVec2(x1 - ol - rim, y1 - ol - rim), fill,
                          r - ol - rim);
        if (on) { /* a leaf marks the one the cursor is on */
            for (Pen p(dl, ImVec2(x0 + r * 1.15f, (y0 + y1) * 0.5f), r * 0.62f); p.next();) {
                p.leaf(0.0f, 0.0f, 1.3f, -PI * 0.25f, kLeaf);
            }
        }

        const char* text = s_choice_text[i];
        float size = bh * 0.42f;
        ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        float room = (x1 - x0) - 2.0f * r;
        if (ts.x > room && ts.x > 0.0f) {
            size *= room / ts.x;
            ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        }
        ImVec2 p((x0 + x1 - ts.x) * 0.5f, (y0 + y1 - ts.y) * 0.5f);
        ImU32 ink = kInk;
        dl->AddText(font, size, p, ink, text);
        dl->AddText(font, size, ImVec2(p.x + size * 0.04f, p.y), ink, text); /* (bolder) */
    }
}

static bool SheetWanted() {
    static int wanted = -1;
    if (wanted < 0) {
        const char* e = getenv("PC_TOUCH_ICON_SHEET");
        wanted = e != nullptr && e[0] == '1';
    }
    return wanted != 0;
}

bool TouchHudWanted() {
    PCTouchHudIcon icons[4];
    int cursor;
    if (SheetWanted()) {
        return true;
    }
    if (!pc_touch_full_on()) {
        return false;
    }
    return pc_touch_full_hud(icons, 4) > 0 || pc_portrait_choices(s_choice_text, 8, &cursor) > 0;
}

void TouchHud() {
    static PCTouchHudIcon icons[64];
    int n = pc_touch_full_hud(icons, 64);
    int cursor = 0;
    int choices = pc_portrait_choices(s_choice_text, 8, &cursor);
    if (SheetWanted()) {
        IconSheet(ImGui::GetBackgroundDrawList(), ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
        return;
    }
    if (n <= 0 && choices <= 0) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float U = W < H ? W : H;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    if (choices > 0) {
        DrawPortraitChoices(dl, choices, cursor, W, H);
    }

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
            float ol = 2.5f * g_scale;
            ImVec2 a(p.x - pad * 1.4f, p.y - pad * 0.8f), b(p.x + ts.x + pad * 1.4f, p.y + ts.y + pad * 0.8f);
            dl->AddRectFilled(ImVec2(a.x - ol, a.y - ol), ImVec2(b.x + ol, b.y + ol), kInk, 14.0f * g_scale + ol);
            dl->AddRectFilled(a, b, kCream, 14.0f * g_scale);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), p, kInk, hint, nullptr, W * 0.6f);
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
            /* a wooden board, with the arrows of a swipe at its ends */
            float bw = r * 3.55f, bh2 = r * 0.74f, ol = r * 0.06f;
            dl->AddRectFilled(ImVec2(c.x - bw, c.y - bh2 + r * 0.13f), ImVec2(c.x + bw, c.y + bh2 + r * 0.13f),
                              Rgb(0x3A, 0x24, 0x14, 0x66), bh2);
            dl->AddRectFilled(ImVec2(c.x - bw, c.y - bh2), ImVec2(c.x + bw, c.y + bh2), kInk, bh2);
            dl->AddRectFilled(ImVec2(c.x - bw + ol, c.y - bh2 + ol), ImVec2(c.x + bw - ol, c.y + bh2 - ol), kWood,
                              bh2 - ol);
            dl->AddLine(ImVec2(c.x - bw + bh2, c.y - bh2 + ol * 2.6f), ImVec2(c.x + bw - bh2, c.y - bh2 + ol * 2.6f),
                        Rgb(0xFF, 0xFF, 0xFF, 0x50), ol * 1.2f);
            for (int side_i = -1; side_i <= 1; side_i += 2) {
                for (Pen p(dl, ImVec2(c.x + side_i * r * 3.02f, c.y), r * 0.36f); p.next();) {
                    p.poly({ { side_i * 0.55f, 0.0f }, { -side_i * 0.35f, -0.62f }, { -side_i * 0.35f, 0.62f } }, kCream);
                }
            }
        }
    }
    for (int i = 0; i < n; i++) {
        DrawIcon(dl, icons[i], W, H, U);
    }
}

} // namespace gui
