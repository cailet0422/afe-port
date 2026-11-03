/* pc_gui.cpp - see pc_gui.h: Dear ImGui set up over the game's window, input,
 * the theme and the shared widgets */
#include "pc_gui_internal.h"

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

extern "C" {
#include "pc_platform.h"
#include "pc_pause_menu.h"
#include "pc_ko.h"
#include "pc_texture_pack.h"
#include "pc_keybindings.h"
extern void pc_gx_restore_after_nes(void);
/* pc/res/gui_font.ttf, embedded by pc/tools/bin2c.py */
extern unsigned char pc_gui_font_ttf[];
extern unsigned int pc_gui_font_ttf_size;
}

#include <math.h>
#include <string.h>

namespace gui {

float g_scale = 1.0f;
bool g_touch = false;

static bool s_ready = false;
static bool s_failed = false;
static ImGuiStyle s_base_style;
static float s_style_scale = 0.0f;
static int s_lang = -1; /* resolved automatic language: 1 Korean, 2 English */
static SDL_FingerID s_finger = 0;
static bool s_finger_down = false;
static bool s_finger_press_pending = false;
static bool s_drew_last_frame = false;

/* --- Language ---------------------------------------------------------- */

static bool system_korean() {
#if SDL_VERSION_ATLEAST(2, 0, 14)
    SDL_Locale* locales = SDL_GetPreferredLocales();
    bool ko = false;

    if (locales != NULL) {
        ko = locales[0].language != NULL && strcmp(locales[0].language, "ko") == 0;
        SDL_free(locales);
    }
    return ko;
#else
    return false;
#endif
}

bool Korean() {
    if (g_pc_settings.ui_language == 1) return true;
    if (g_pc_settings.ui_language == 2) return false;
    if (s_lang < 0) {
        s_lang = (g_pc_ko_mode || system_korean()) ? 1 : 2;
    }
    return s_lang == 1;
}

const char* T(const char* en, const char* ko) {
    return Korean() ? ko : en;
}

/* --- Theme ------------------------------------------------------------- */

/* Leaf green on paper, like the game's own windows */
ImU32 ColAccent() { return IM_COL32(0x4C, 0xA8, 0x5C, 0xFF); }
ImU32 ColText() { return IM_COL32(0x4A, 0x3B, 0x2A, 0xFF); }
ImU32 ColMuted() { return IM_COL32(0x8A, 0x7B, 0x66, 0xFF); }
ImU32 ColWarn() { return IM_COL32(0xD0, 0x6A, 0x1E, 0xFF); }
ImU32 ColGood() { return IM_COL32(0x2E, 0x8B, 0x45, 0xFF); }

ImVec4 Vec(ImU32 c) {
    return ImGui::ColorConvertU32ToFloat4(c);
}

static void build_base_style() {
    ImGuiStyle& s = s_base_style;
    ImVec4* c = s.Colors;

    s = ImGuiStyle();
    s.WindowPadding = ImVec2(18, 16);
    s.FramePadding = ImVec2(12, 7);
    s.ItemSpacing = ImVec2(10, 9);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 16;
    s.GrabMinSize = 18;
    s.WindowRounding = 16;
    s.ChildRounding = 12;
    s.FrameRounding = 10;
    s.PopupRounding = 12;
    s.ScrollbarRounding = 10;
    s.GrabRounding = 10;
    s.TabRounding = 10;
    s.WindowBorderSize = 2;
    s.FrameBorderSize = 0;
    s.PopupBorderSize = 2;
    s.TabBarBorderSize = 2;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    s.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    s.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    s.SeparatorTextBorderSize = 2;

    c[ImGuiCol_Text] = Vec(ColText());
    c[ImGuiCol_TextDisabled] = Vec(ColMuted());
    c[ImGuiCol_WindowBg] = ImVec4(1.00f, 0.973f, 0.906f, 0.97f);
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = ImVec4(1.00f, 0.980f, 0.925f, 0.99f);
    c[ImGuiCol_Border] = ImVec4(0.70f, 0.60f, 0.42f, 0.70f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(0.94f, 0.89f, 0.78f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.91f, 0.85f, 0.71f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.87f, 0.80f, 0.64f, 1.00f);
    c[ImGuiCol_TitleBg] = ImVec4(0.30f, 0.66f, 0.36f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.30f, 0.66f, 0.36f, 1.00f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.94f, 0.89f, 0.78f, 0.60f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.72f, 0.63f, 0.47f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.64f, 0.55f, 0.40f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.55f, 0.47f, 0.33f, 1.00f);
    c[ImGuiCol_CheckMark] = ImVec4(0.18f, 0.55f, 0.27f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.66f, 0.36f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.22f, 0.55f, 0.28f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.80f, 0.92f, 0.74f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.68f, 0.87f, 0.62f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.80f, 0.50f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.80f, 0.92f, 0.74f, 1.00f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.68f, 0.87f, 0.62f, 1.00f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.55f, 0.80f, 0.50f, 1.00f);
    c[ImGuiCol_Separator] = ImVec4(0.78f, 0.69f, 0.52f, 0.70f);
    c[ImGuiCol_SeparatorHovered] = c[ImGuiCol_Separator];
    c[ImGuiCol_SeparatorActive] = c[ImGuiCol_Separator];
    c[ImGuiCol_Tab] = ImVec4(0.92f, 0.86f, 0.72f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(0.68f, 0.87f, 0.62f, 1.00f);
    c[ImGuiCol_TabSelected] = ImVec4(0.30f, 0.66f, 0.36f, 1.00f);
    c[ImGuiCol_TabSelectedOverline] = ImVec4(0.18f, 0.48f, 0.24f, 1.00f);
    c[ImGuiCol_TabDimmed] = c[ImGuiCol_Tab];
    c[ImGuiCol_TabDimmedSelected] = c[ImGuiCol_TabSelected];
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.92f, 0.86f, 0.72f, 1.00f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.78f, 0.69f, 0.52f, 1.00f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.86f, 0.79f, 0.64f, 1.00f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(0.94f, 0.89f, 0.78f, 0.45f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.55f, 0.80f, 0.50f, 0.55f);
    c[ImGuiCol_NavCursor] = ImVec4(0.93f, 0.55f, 0.15f, 1.00f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.10f, 0.08f, 0.05f, 0.45f);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.30f, 0.66f, 0.36f, 1.00f);
}

/* 1 on a 1280x720 desktop window; follows the window's height, a little
 * bigger for fingers; settings.ini ui_scale overrides */
static float wanted_scale() {
    float h = (float)(g_pc_window_h > 0 ? g_pc_window_h : 720);
    float w = (float)(g_pc_window_w > 0 ? g_pc_window_w : 1280);
    float s = fminf(h / 720.0f, w / 1000.0f);

#ifdef __ANDROID__
    s *= 1.25f;
#endif
    if (g_pc_settings.ui_scale > 0) {
        s *= (float)g_pc_settings.ui_scale / 100.0f;
    }
    if (s < 0.6f) s = 0.6f;
    if (s > 4.0f) s = 4.0f;
    return s;
}

static void apply_scale() {
    float s = wanted_scale();

    if (fabsf(s - s_style_scale) < 0.01f) {
        return;
    }
    ImGuiStyle style = s_base_style;
    style.ScaleAllSizes(s);
    style.FontScaleDpi = s;
    style.FontSizeBase = 20.0f;
    ImGui::GetStyle() = style;
    s_style_scale = s;
    g_scale = s;
}

static void set_ime_data(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData* data);

static bool init() {
    if (s_ready || s_failed) {
        return s_ready;
    }
    if (g_pc_window == NULL || g_pc_gl_context == NULL) {
        return false;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL; /* nothing to remember between runs */
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
#ifdef __ANDROID__
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    g_touch = true;
#endif

    if (!ImGui_ImplSDL2_InitForOpenGL(g_pc_window, g_pc_gl_context)) {
        s_failed = true;
        ImGui::DestroyContext();
        return false;
    }
    ImGui::GetPlatformIO().Platform_SetImeDataFn = set_ime_data;
#ifdef PC_GLES
    const char* glsl = "#version 300 es";
#else
    const char* glsl = "#version 330 core";
#endif
    if (!ImGui_ImplOpenGL3_Init(glsl)) {
        s_failed = true;
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    snprintf(cfg.Name, sizeof(cfg.Name), "AFE Menu Gothic");
    if (io.Fonts->AddFontFromMemoryTTF(pc_gui_font_ttf, (int)pc_gui_font_ttf_size, 20.0f, &cfg) == NULL) {
        io.Fonts->AddFontDefault();
    }

    build_base_style();
    s_style_scale = 0.0f;
    apply_scale();
    s_ready = true;
    printf("[GUI] Dear ImGui %s ready (scale %.2f)\n", IMGUI_VERSION, g_scale);
    return true;
}

/* The system's text input (and the phone's keyboard) only while a field of
 * the menus has the focus, put back as it was after: the game's text editor
 * turns it off for the built-in Hangul input, or on for the system's */
static bool s_ime_on = false;
static bool s_ime_was_active = false;

static void set_ime_data(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData* data) {
    if (data->WantVisible) {
        SDL_Rect r;

        r.x = (int)data->InputPos.x;
        r.y = (int)data->InputPos.y;
        r.w = 1;
        r.h = (int)data->InputLineHeight;
        SDL_SetTextInputRect(&r);
        if (!s_ime_on) {
            s_ime_was_active = SDL_IsTextInputActive() == SDL_TRUE;
            SDL_StartTextInput();
            s_ime_on = true;
        }
    } else if (s_ime_on) {
        s_ime_on = false;
        if (!s_ime_was_active) {
            SDL_StopTextInput();
        }
    }
}

/* --- Widgets ----------------------------------------------------------- */

float ButtonHeight() {
    return ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f + (g_touch ? 10.0f * g_scale : 4.0f * g_scale);
}

bool BigButton(const char* label, float width, bool primary) {
    if (width < 0.0f) {
        width = ImGui::GetContentRegionAvail().x;
    }
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.66f, 0.36f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.73f, 0.42f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.56f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    }
    bool pressed = ImGui::Button(label, ImVec2(width, ButtonHeight()));
    if (primary) {
        ImGui::PopStyleColor(4);
    }
    return pressed;
}

bool SmallButton(const char* label) {
    return ImGui::Button(label, ImVec2(0, g_touch ? ButtonHeight() * 0.85f : 0.0f));
}

void RowLabel(const char* label, const char* help) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (help != NULL && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", help);
    }
}

bool Switch(const char* label, int* value, const char* help) {
    ImGuiStyle& style = ImGui::GetStyle();
    float h = ImGui::GetFrameHeight();
    float w = h * 1.9f;
    bool changed = false;

    ImGui::PushID(label);
    RowLabel(label, help);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - w - style.WindowPadding.x * 0.2f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##switch", ImVec2(w, h))) {
        *value = !*value;
        changed = true;
    }
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = *value ? 1.0f : 0.0f;
    ImU32 bg = *value ? (hovered ? IM_COL32(0x5C, 0xBA, 0x6B, 0xFF) : ColAccent())
                      : (hovered ? IM_COL32(0xC9, 0xBC, 0xA2, 0xFF) : IM_COL32(0xD6, 0xCA, 0xB2, 0xFF));
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h * 0.5f);
    dl->AddCircleFilled(ImVec2(p.x + h * 0.5f + t * (w - h), p.y + h * 0.5f), h * 0.5f - 3.0f * g_scale,
                        IM_COL32(0xFF, 0xFF, 0xFF, 0xFF));
    ImGui::PopID();
    return changed;
}

void Help(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, Vec(ColMuted()));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void Heading(const char* text) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, Vec(IM_COL32(0x2E, 0x7D, 0x3E, 0xFF)));
    ImGui::SeparatorText(text);
    ImGui::PopStyleColor();
}

void TouchScroll() {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiContext& g = *GImGui;

    if (io.MouseSource != ImGuiMouseSource_TouchScreen || !ImGui::IsMouseDown(0)) {
        return;
    }
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        return;
    }
    ImVec2 drag = ImGui::GetMouseDragDelta(0, 0.0f);
    if (fabsf(drag.y) < 12.0f * g_scale || fabsf(drag.y) < fabsf(drag.x) * 1.2f) {
        return;
    }
    /* a vertical drag scrolls; the button it started on no longer presses */
    if (g.ActiveId != 0 && g.ActiveIdWindow != NULL &&
        (g.ActiveIdWindow == ImGui::GetCurrentWindow() || g.ActiveIdWindow->RootWindow == ImGui::GetCurrentWindow()->RootWindow)) {
        ImGui::ClearActiveID();
    }
    ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
}

void Progress(int permille) {
    char buf[16];

    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    snprintf(buf, sizeof(buf), "%d%%", permille / 10);
    ImGui::ProgressBar((float)permille / 1000.0f, ImVec2(-1, 0), buf);
}

void Link(const char* text, const char* url) {
    ImGui::PushStyleColor(ImGuiCol_Text, Vec(IM_COL32(0x1F, 0x6F, 0xB5, 0xFF)));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y), b, IM_COL32(0x1F, 0x6F, 0xB5, 0xFF));
        ImGui::SetTooltip("%s", url);
    }
    if (ImGui::IsItemClicked()) {
#if SDL_VERSION_ATLEAST(2, 0, 14)
        SDL_OpenURL(url);
#endif
    }
}

void CenterNext(float width) {
    float avail = ImGui::GetContentRegionAvail().x;

    if (avail > width) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - width) * 0.5f);
    }
}

/* --- Notifications ----------------------------------------------------- */

/* (no std::string here: the decomp's libc headers come first) */
struct Note {
    char text[768];
    Uint32 until;
};
#define MAX_NOTES 4
static Note s_notes[MAX_NOTES];
static int s_note_count = 0;

static void drop_note() {
    memmove(&s_notes[0], &s_notes[1], sizeof(Note) * (MAX_NOTES - 1));
    s_note_count--;
}

bool NotificationsWanted() {
    Uint32 now = SDL_GetTicks();

    while (s_note_count > 0 && SDL_TICKS_PASSED(now, s_notes[0].until)) {
        drop_note();
    }
    return s_note_count > 0;
}

void DrawNotifications() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float y = vp->Pos.y + 14.0f * g_scale;
    float pad = 10.0f * g_scale;

    for (int i = 0; i < s_note_count; i++) {
        const Note& n = s_notes[i];
        float wrap = vp->Size.x * 0.85f - pad * 2.0f; /* (a long one in lines) */
        ImVec2 size = ImGui::CalcTextSize(n.text, NULL, false, wrap);
        float x = vp->Pos.x + (vp->Size.x - size.x) * 0.5f;
        Uint32 left = n.until - SDL_GetTicks();
        int alpha = left < 500 ? (int)(left * 255 / 500) : 255;

        dl->AddRectFilled(ImVec2(x - pad, y - pad * 0.6f), ImVec2(x + size.x + pad, y + size.y + pad * 0.6f),
                          IM_COL32(0xFF, 0xF8, 0xE7, alpha * 235 / 255), 12.0f * g_scale);
        dl->AddRect(ImVec2(x - pad, y - pad * 0.6f), ImVec2(x + size.x + pad, y + size.y + pad * 0.6f),
                    IM_COL32(0x4C, 0xA8, 0x5C, alpha), 12.0f * g_scale, 0, 2.0f * g_scale);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, y), IM_COL32(0x4A, 0x3B, 0x2A, alpha), n.text,
                    NULL, wrap);
        y += size.y + pad * 2.0f;
    }
}

} // namespace gui

using namespace gui;

/* --- Input ------------------------------------------------------------- */

static bool menu_open() {
    return g_pc_paused != 0;
}

/* The chat box while playing (pc_gui_mp.cpp) */
static bool chat_open() {
    return !menu_open() && ChatIsOpen();
}

/* The key (or mouse button) bound to Chat */
static bool chat_key_event(const SDL_Event* e) {
    PCInputCode code = g_pc_keybindings.key[PC_BIND_CHAT];

    if (code < 0) {
        return false;
    }
    if (e->type == SDL_KEYDOWN) {
        return !(code & PC_INPUT_MOUSE_BIT) && e->key.keysym.scancode == (SDL_Scancode)code &&
               (e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) == 0;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN) {
        return (code & PC_INPUT_MOUSE_BIT) && e->button.which != SDL_TOUCH_MOUSEID &&
               e->button.button == (Uint8)(code & 0xFF);
    }
    return false;
}

/* The letter the Chat key types right after its press is not for the box */
static bool s_chat_drop_text = false;
static bool s_chat_key_held = false;

extern "C" int pc_gui_handle_event(const SDL_Event* e) {
    if (!init()) {
        return 0;
    }
    bool menu = menu_open();

    /* The bindings page takes the next key or button as it is */
    if (menu && MenuCapturing()) {
        switch (e->type) {
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
            case SDL_CONTROLLERAXISMOTION:
            case SDL_TEXTINPUT:
                MenuCaptureEvent(e);
                return 1;
            default:
                break;
        }
    }

    bool chat = chat_open();

    /* the Chat key (T) opens the chat box in a room */
    if (!menu && !chat && chat_key_event(e) && ChatAllowed()) {
        if (e->type == SDL_KEYDOWN && e->key.repeat) {
            return 0;
        }
        ChatOpen();
        s_chat_drop_text = e->type == SDL_KEYDOWN;
        s_chat_key_held = e->type == SDL_KEYDOWN;
        return 1;
    }

    switch (e->type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (!menu && chat) {
                if (e->type == SDL_KEYDOWN && !e->key.repeat &&
                    (e->key.keysym.sym == SDLK_ESCAPE || e->key.keysym.sym == SDLK_AC_BACK)) {
                    ChatClose();
                    return 1;
                }
                if (s_chat_key_held && e->key.keysym.scancode == (SDL_Scancode)g_pc_keybindings.key[PC_BIND_CHAT]) {
                    /* the press that opened the box, still held: not typed */
                    if (e->type == SDL_KEYUP) {
                        s_chat_key_held = false;
                    } else {
                        s_chat_drop_text = true;
                        return 1;
                    }
                }
                ImGui_ImplSDL2_ProcessEvent(e);
                return 1;
            }
            if (!menu) {
                if (e->type == SDL_KEYUP) {
                    ImGui_ImplSDL2_ProcessEvent(e); /* no key stays down in the menus */
                }
                return 0;
            }
            if (e->type == SDL_KEYDOWN && !e->key.repeat &&
                (e->key.keysym.sym == SDLK_ESCAPE || e->key.keysym.sym == SDLK_AC_BACK)) {
                if (!MenuBack()) {
                    pc_pause_menu_toggle();
                }
                return 1;
            }
            ImGui_ImplSDL2_ProcessEvent(e);
            return 1;
        case SDL_TEXTINPUT:
        case SDL_TEXTEDITING:
            if (!menu && chat && s_chat_drop_text) {
                if (e->type == SDL_TEXTINPUT) {
                    s_chat_drop_text = false;
                }
                return 1;
            }
            if (!menu && !chat) {
                return 0;
            }
            ImGui_ImplSDL2_ProcessEvent(e);
            return 1;
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL: {
            Uint32 which = e->type == SDL_MOUSEMOTION ? e->motion.which
                         : e->type == SDL_MOUSEWHEEL  ? e->wheel.which
                                                      : e->button.which;
            if (which == SDL_TOUCH_MOUSEID) {
                return menu || chat ? 1 : 0; /* fingers come as fingers (pc_gui_touch) */
            }
            if (e->type != SDL_MOUSEWHEEL) {
                g_touch = false;
            }
            ImGui_ImplSDL2_ProcessEvent(e);
            if (!menu && chat && e->type == SDL_MOUSEBUTTONDOWN && !ChatWindowContains((float)e->button.x, (float)e->button.y)) {
                ChatClose(); /* a click outside the box */
            }
            return menu || chat ? 1 : 0;
        }
        case SDL_CONTROLLERBUTTONDOWN:
            if (!menu && chat) {
                if (e->cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                    ChatClose();
                }
                return 1;
            }
            if (!menu) {
                return 0;
            }
            /* B and Start go back; the rest the backend reads from the pad */
            if (e->cbutton.button == SDL_CONTROLLER_BUTTON_B || e->cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                if (e->cbutton.button == SDL_CONTROLLER_BUTTON_START || !MenuBack()) {
                    pc_pause_menu_toggle();
                }
            } else {
                /* also for --input-script PAD, which sends no real pad state */
                ImGuiKey key = ImGuiKey_None;

                switch (e->cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_A: key = ImGuiKey_GamepadFaceDown; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP: key = ImGuiKey_GamepadDpadUp; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: key = ImGuiKey_GamepadDpadDown; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: key = ImGuiKey_GamepadDpadLeft; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: key = ImGuiKey_GamepadDpadRight; break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: key = ImGuiKey_GamepadL1; break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: key = ImGuiKey_GamepadR1; break;
                    default: break;
                }
                if (key != ImGuiKey_None && SDL_GameControllerFromInstanceID(e->cbutton.which) == NULL) {
                    ImGui::GetIO().AddKeyEvent(key, true);
                    ImGui::GetIO().AddKeyEvent(key, false);
                }
            }
            return 1;
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERAXISMOTION:
            return menu || chat ? 1 : 0;
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
        case SDL_WINDOWEVENT:
            ImGui_ImplSDL2_ProcessEvent(e);
            return 0;
        default:
            return 0;
    }
}

extern "C" int pc_gui_touch(int kind, s64 id, float x, float y) {
    if (!init()) {
        return 0;
    }
    ImGuiIO& io = ImGui::GetIO();
    int ww = 0, wh = 0;

    SDL_GetWindowSize(g_pc_window, &ww, &wh);
    bool takes = menu_open() || chat_open();

    if (kind == 0) {
        if (!takes) {
            /* the chat button (touch screens, in a room) */
            if (!menu_open() && ChatAllowed() && ChatButtonHit(x * ww, y * wh)) {
                ChatOpen();
                return 1;
            }
            return 0;
        }
        if (s_finger_down) {
            return 1;
        }
        if (!menu_open() && !ChatWindowContains(x * ww, y * wh)) {
            ChatClose(); /* a tap outside the box */
            return 1;
        }
        s_finger = (SDL_FingerID)id;
        s_finger_down = true;
        s_finger_press_pending = true; /* pressed on the next frame: tabs want a hover first */
        g_touch = true;
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(x * ww, y * wh);
        return 1;
    }
    if (!s_finger_down || (SDL_FingerID)id != s_finger) {
        return takes ? 1 : 0;
    }
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    if (kind == 1) {
        io.AddMousePosEvent(x * ww, y * wh);
    } else {
        if (s_finger_press_pending) {
            io.AddMouseButtonEvent(0, true);
            s_finger_press_pending = false;
        }
        io.AddMouseButtonEvent(0, false);
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); /* nothing stays hovered under a lifted finger */
        s_finger_down = false;
    }
    return 1;
}

extern "C" int pc_gui_wants_keyboard(void) {
    return s_ready && ((menu_open() && MenuWantsKeyboard()) || chat_open());
}

extern "C" int pc_gui_chat_active(void) {
    return s_ready && chat_open() ? 1 : 0;
}

extern "C" void pc_gui_chat_request(void) {
    if (s_ready && !menu_open() && ChatAllowed()) {
        ChatOpen();
    }
}

extern "C" void pc_gui_notify(const char* text) {
    if (s_note_count == MAX_NOTES) {
        drop_note();
    }
    Note& n = s_notes[s_note_count++];
    snprintf(n.text, sizeof(n.text), "%s", text);
    /* 4 s, longer for a long one (to read) */
    n.until = SDL_GetTicks() + 4000 + (Uint32)(strlen(n.text) > 120 ? (strlen(n.text) - 120) * 30 : 0);
    if (n.until - SDL_GetTicks() > 15000) n.until = SDL_GetTicks() + 15000;
    printf("[GUI] %s\n", text);
}

extern "C" int pc_gui_korean(void) {
    return Korean() ? 1 : 0;
}

extern "C" void pc_gui_menu_opened(void) {
    MenuOpen();
}

extern "C" void pc_gui_menu_closed(void) {
    MenuClose();
}

extern "C" void pc_gui_menu_open_update(void) {
    MenuOpenUpdate();
}

/* --- Frame ------------------------------------------------------------- */

extern "C" void pc_gui_frame(void) {
    if (!init()) {
        return;
    }
    bool menu = menu_open();
    bool notes = NotificationsWanted();
    bool mp = MultiplayerOverlayWanted();

    if ((menu || !mp) && ChatIsOpen()) {
        ChatClose(); /* the room ended or the menu opened (it has the chat too) */
    }

    if (!menu && !notes && !mp && !s_drew_last_frame) {
        return;
    }
    s_drew_last_frame = menu || notes || mp;

    apply_scale();
    if (s_finger_press_pending) {
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        s_finger_press_pending = false;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (mp && !menu) {
        MultiplayerOverlay();
    }
    if (menu) {
        MenuDraw();
    }
    if (notes) {
        DrawNotifications();
    }

    ImGui::Render();
    if (!g_pc_skip_draw) {
        glViewport(0, 0, g_pc_window_w, g_pc_window_h);
        glDisable(GL_SCISSOR_TEST);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        pc_gx_restore_after_nes();
    }
    s_chat_drop_text = false; /* (only the events of the Chat key's frame) */
}
