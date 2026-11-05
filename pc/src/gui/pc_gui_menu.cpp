/* pc_gui_menu.cpp - the pause menu and the settings pages (see pc_gui.h) */
#include "pc_gui_internal.h"

extern "C" {
#include "pc_autosave.h"
#include "pc_platform.h"
#include "pc_pause_menu.h"
#include "pc_settings.h"
#include "pc_settings_menu.h"
#include "pc_keybindings.h"
#include "pc_ko.h"
#include "pc_ko_patch.h"
#include "pc_rom_dir.h"
#include "pc_update.h"
}

#include <string.h>

namespace gui {

enum Page {
    PAGE_MAIN,
    PAGE_SETTINGS,
    PAGE_MULTIPLAYER,
};

enum Tab {
    TAB_VIDEO,
    TAB_AUDIO,
    TAB_GAMEPLAY,
    TAB_CONTROLS,
    TAB_DISC,
    TAB_TEXTURES,
    TAB_INTERFACE,
    TAB_ABOUT,
    TAB_COUNT
};

static Page s_page = PAGE_MAIN;
static int s_tab = TAB_VIDEO;
static int s_tab_request = -1; /* select this tab on the next frame */
static bool s_focus_first = false;

/* popups */
static bool s_quit_open = false;
static bool s_update_open = false;
static bool s_update_resume = false; /* the update page opened by itself: closing it resumes the game */
static bool s_patch_open = false;
/* Settings > Gameplay > Town data: the list, then a town to delete (0 = card A, 1 = B) */
static bool s_town_open = false;
static int s_town_chan = -1;
static bool s_town_have[2];
static char s_town_name[2][64];
static char s_town_players[2][160];
static bool s_res_open = false;
static Uint32 s_res_deadline = 0;
static int s_res_old_w, s_res_old_h, s_res_old_fullscreen;

/* the settings the game started with: some take effect only after a restart */
static PCSettings s_startup;
static bool s_startup_taken = false;

/* bindings capture: the action and column (0 keyboard, 1 gamepad) */
static int s_capture = -1;
static int s_capture_col = 0;
static Uint32 s_capture_deadline = 0;
#define CAPTURE_MS 6000u
#define RES_CONFIRM_MS 15000u

/* --- Open / close / back ------------------------------------------------ */

void MenuOpen() {
    if (!s_startup_taken) {
        s_startup = g_pc_settings;
        s_startup_taken = true;
    }
    s_page = PAGE_MAIN;
    s_focus_first = true;
    s_quit_open = false;
    s_town_open = false;
    s_capture = -1;
}

void MenuClose() {
    s_capture = -1;
    s_quit_open = false;
    s_town_open = false;
    s_update_open = false;
    s_update_resume = false;
    if (s_res_open) {
        s_res_open = false; /* keep what was chosen */
        pc_settings_save();
    }
}

void MenuOpenUpdate() {
    s_update_open = true;
    s_update_resume = true;
}

bool MenuCapturing() {
    return s_capture >= 0;
}

bool MenuWantsKeyboard() {
    return ImGui::GetIO().WantTextInput || s_capture >= 0;
}

static void update_close() {
    s_update_open = false;
    if (s_update_resume) {
        s_update_resume = false;
        if (g_pc_paused) {
            pc_pause_menu_toggle();
        }
    }
}

static void res_keep() {
    s_res_open = false;
    s_res_deadline = 0;
    pc_settings_save();
}

static void res_revert() {
    g_pc_settings.window_width = s_res_old_w;
    g_pc_settings.window_height = s_res_old_h;
    g_pc_settings.fullscreen = s_res_old_fullscreen;
    pc_settings_apply();
    pc_settings_save();
    s_res_open = false;
    s_res_deadline = 0;
}

bool MenuBack() {
    if (s_capture >= 0) {
        s_capture = -1;
        return true;
    }
    if (s_res_open) {
        res_revert();
        return true;
    }
    if (s_quit_open) {
        s_quit_open = false;
        return true;
    }
    if (s_patch_open) {
        if (pc_ko_patch_state() == PC_KO_PATCH_RUNNING) {
            pc_ko_patch_cancel();
        } else {
            pc_ko_patch_reset();
            s_patch_open = false;
        }
        return true;
    }
    if (s_update_open) {
        if (pc_update_state() == PC_UPDATE_DOWNLOADING) {
            pc_update_cancel();
        } else {
            bool resume = s_update_resume;

            update_close();
            if (resume) {
                return true; /* already resumed */
            }
        }
        return true;
    }
    if (TexturePackBack() || MultiplayerBack()) {
        return true;
    }
    if (s_page != PAGE_MAIN) {
        s_page = PAGE_MAIN;
        s_focus_first = true;
        return true;
    }
    return false;
}

/* --- Bindings capture ---------------------------------------------------- */

void MenuCaptureEvent(const SDL_Event* e) {
    int action = s_capture;

    if (action < 0) {
        return;
    }
    switch (e->type) {
        case SDL_KEYDOWN:
            if (e->key.repeat) {
                break;
            }
            if (e->key.keysym.sym == SDLK_ESCAPE || e->key.keysym.sym == SDLK_AC_BACK) {
                s_capture = -1;
            } else if (s_capture_col == 0 && e->key.keysym.scancode != SDL_SCANCODE_UNKNOWN) {
                pc_keybindings_set_key(action, (PCInputCode)e->key.keysym.scancode);
                pc_keybindings_save();
                s_capture = -1;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (s_capture_col == 0 && e->button.which != SDL_TOUCH_MOUSEID && e->button.button >= SDL_BUTTON_LEFT &&
                e->button.button <= SDL_BUTTON_MIDDLE) {
                pc_keybindings_set_key(action, PC_INPUT_MOUSE_BIT | e->button.button);
                pc_keybindings_save();
                s_capture = -1;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (s_capture_col == 1) {
                pc_keybindings_set_pad(action, (PCPadInput)e->cbutton.button);
                pc_keybindings_save();
            }
            s_capture = -1; /* waiting for a key: a pad button gives up */
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (s_capture_col == 1 && e->caxis.value > 16384 &&
                (e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
                pc_keybindings_set_pad(action, e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? PC_PAD_TRIGGER_LEFT
                                                                                               : PC_PAD_TRIGGER_RIGHT);
                pc_keybindings_save();
                s_capture = -1;
            }
            break;
        default:
            break;
    }
}

/* --- Helpers -------------------------------------------------------------- */

static void save() {
    pc_settings_save();
}

static bool restart_needed() {
    return g_pc_settings.msaa != s_startup.msaa || g_pc_settings.preload_textures != s_startup.preload_textures ||
           g_pc_settings.renderer != s_startup.renderer ||
           pc_rom_dir_changed() || g_pc_settings.korean != s_startup.korean ||
           strcmp(g_pc_settings.ko_image, s_startup.ko_image) != 0;
}

/* The page window: centred, most of the screen on small ones */
static bool begin_page(const char* id, float want_w, float want_h, bool auto_height) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float w = want_w * g_scale;
    float h = want_h * g_scale;

    if (w > vp->Size.x * 0.94f) w = vp->Size.x * 0.94f;
    if (h > vp->Size.y * 0.92f) h = vp->Size.y * 0.92f;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    if (auto_height) {
        ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, vp->Size.y * 0.92f));
    } else {
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    }
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (auto_height) {
        flags |= ImGuiWindowFlags_AlwaysAutoResize;
    }
    return ImGui::Begin(id, NULL, flags);
}

static void title(const char* text, const char* sub = nullptr) {
    ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * 1.45f);
    float w = ImGui::CalcTextSize(text).x;
    CenterNext(w);
    ImGui::PushStyleColor(ImGuiCol_Text, Vec(IM_COL32(0x2E, 0x7D, 0x3E, 0xFF)));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (sub != NULL) {
        float sw = ImGui::CalcTextSize(sub).x;
        CenterNext(sw);
        ImGui::TextDisabled("%s", sub);
    }
    ImGui::Spacing();
}

/* "< Back" and the page's name on one row */
static bool header(const char* name) {
    bool back = SmallButton(T("< Back", "< 뒤로"));
    ImGui::SameLine();
    ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * 1.3f);
    float w = ImGui::CalcTextSize(name).x;
    ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x + ImGui::GetStyle().WindowPadding.x - w) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, Vec(IM_COL32(0x2E, 0x7D, 0x3E, 0xFF)));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Separator();
    return back;
}

/* Two columns: label on the left, the control filling the right */
static float s_label_w = 0.0f;

static void row(const char* label, const char* help = nullptr) {
    RowLabel(label, help);
    ImGui::SameLine(s_label_w);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

/* --- Main page ------------------------------------------------------------ */

static void draw_main() {
    char buf[128];

    if (!begin_page("##pause", 460, 0, true)) {
        ImGui::End();
        return;
    }
    snprintf(buf, sizeof(buf), "afe-port v%s", pc_update_version());
    title(T("Paused", "일시정지"), buf);

    if (s_focus_first) {
        ImGui::SetKeyboardFocusHere();
        s_focus_first = false;
    }
    if (BigButton(T("Resume", "계속하기"), -1, true)) {
        pc_pause_menu_toggle();
    }
    const char* mp = MultiplayerStatus();
    if (mp != NULL) {
        snprintf(buf, sizeof(buf), "%s  (%s)###mp", T("Multiplayer", "멀티플레이"), mp);
    } else {
        snprintf(buf, sizeof(buf), "%s###mp", T("Multiplayer", "멀티플레이"));
    }
    if (BigButton(buf)) {
        s_page = PAGE_MULTIPLAYER;
    }
    if (BigButton(T("Settings", "설정"))) {
        s_page = PAGE_SETTINGS;
        s_tab_request = s_tab;
    }
    PCUpdateState us = pc_update_state();
    if (us == PC_UPDATE_AVAILABLE || us == PC_UPDATE_DOWNLOADING || us == PC_UPDATE_INSTALLED) {
        if (us == PC_UPDATE_INSTALLED) {
            snprintf(buf, sizeof(buf), T("Restart for %s###upd", "%s(으)로 다시 시작###upd"), pc_update_new_version());
        } else {
            snprintf(buf, sizeof(buf), T("Update to %s###upd", "%s(으)로 업데이트###upd"), pc_update_new_version());
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.98f, 0.86f, 0.55f, 1.0f));
        if (BigButton(buf)) {
            s_update_open = true;
            s_update_resume = false;
        }
        ImGui::PopStyleColor();
    }
    if (BigButton(T("Quit game", "게임 종료"))) {
        s_quit_open = true;
    }
    ImGui::Spacing();
    if (g_touch) {
        Help(T("Tap outside to go back to the game? Use Resume.", "게임으로 돌아가려면 [계속하기]를 누르세요."));
    } else {
        Help(T("Esc or B: back   Enter or A: choose", "Esc 또는 B: 뒤로   Enter 또는 A: 선택"));
    }
    ImGui::End();
}

/* --- Settings tabs -------------------------------------------------------- */

/* The graphics API (the ones this build has); from the next start */
static void video_api() {
    PCGfxApi apis[3];
    const char* names[3];
    int n = 0, idx = 0;
    char in_use[200];

    for (int a = PC_GFX_OPENGL; a <= PC_GFX_D3D12; a++) {
        if (!pc_gfx_api_built((PCGfxApi)a)) continue;
        if (g_pc_settings.renderer == a) idx = n;
        apis[n] = (PCGfxApi)a;
        names[n++] = pc_gfx_api_name((PCGfxApi)a);
    }
    if (n < 2) return;
    row(T("Graphics API", "그래픽 API"), T("Takes effect after a restart", "다시 시작하면 적용"));
    if (ImGui::Combo("##api", &idx, names, n) && apis[idx] != g_pc_settings.renderer) {
        g_pc_settings.renderer = apis[idx];
        save();
    }
    snprintf(in_use, sizeof(in_use), "%s: %s", T("In use", "사용 중"), pc_gfx_device_name());
    Help(in_use);
    if ((g_pc_renderer_override >= 0 ? g_pc_renderer_override : s_startup.renderer) != (int)g_pc_gfx_api) {
        /* asked for at this start, but it could not start here */
        Help(T("The chosen API could not start on this device, so OpenGL is used.",
               "선택한 API를 이 기기에서 시작할 수 없어 OpenGL을 사용합니다."));
    }
}

static void tab_video() {
    video_api();
#ifndef __ANDROID__
    {
        const char* modes[] = { T("Windowed", "창 모드"), T("Fullscreen", "전체 화면"),
                                T("Borderless", "테두리 없는 창") };
        int mode = g_pc_settings.fullscreen;

        row(T("Display", "화면 모드"));
        if (ImGui::Combo("##display", &mode, modes, 3) && mode != g_pc_settings.fullscreen) {
            s_res_old_w = g_pc_settings.window_width;
            s_res_old_h = g_pc_settings.window_height;
            s_res_old_fullscreen = g_pc_settings.fullscreen;
            g_pc_settings.fullscreen = mode;
            pc_settings_apply();
            s_res_open = true;
            s_res_deadline = SDL_GetTicks() + RES_CONFIRM_MS;
        }
    }
    {
        char cur[32];
        int n = pc_settings_resolution_count();

        snprintf(cur, sizeof(cur), "%d x %d", g_pc_settings.window_width, g_pc_settings.window_height);
        row(T("Resolution", "해상도"));
        if (ImGui::BeginCombo("##res", cur)) {
            for (int i = 0; i < n; i++) {
                int w, h;
                char label[32];

                pc_settings_resolution_at(i, &w, &h);
                snprintf(label, sizeof(label), "%d x %d", w, h);
                bool sel = w == g_pc_settings.window_width && h == g_pc_settings.window_height;
                if (ImGui::Selectable(label, sel) && !sel) {
                    s_res_old_w = g_pc_settings.window_width;
                    s_res_old_h = g_pc_settings.window_height;
                    s_res_old_fullscreen = g_pc_settings.fullscreen;
                    g_pc_settings.window_width = w;
                    g_pc_settings.window_height = h;
                    pc_settings_apply();
                    s_res_open = true;
                    s_res_deadline = SDL_GetTicks() + RES_CONFIRM_MS;
                }
                if (sel) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }
#endif
    if (Switch(T("VSync", "수직 동기화(VSync)"), &g_pc_settings.vsync)) {
        pc_settings_apply();
        save();
    }
    {
        static const int steps[] = { 0, 2, 4, 8 };
        const char* names[] = { T("Off", "끔"), "2x", "4x", "8x" };
        int idx = 0;

        for (int i = 0; i < 4; i++) {
            if (g_pc_settings.msaa == steps[i]) idx = i;
        }
        row(T("Anti-aliasing (MSAA)", "안티에일리어싱(MSAA)"), T("Takes effect after a restart", "다시 시작하면 적용"));
        if (ImGui::Combo("##msaa", &idx, names, 4)) {
            g_pc_settings.msaa = steps[idx];
            save();
        }
    }
    Help(T("The graphics API and anti-aliasing take effect after a restart. HD textures are under \"HD textures\".",
           "그래픽 API와 안티에일리어싱은 다시 시작하면 적용됩니다. HD 텍스처는 [HD 텍스처] 탭에 있습니다."));
}

static void tab_audio() {
    row(T("Master volume", "전체 음량"));
    if (ImGui::SliderInt("##vol", &g_pc_settings.master_volume, 0, 100, "%d%%")) {
        /* the mixer reads it every sound frame */
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        save();
    }
}

static void tab_gameplay() {
    int resetti = !g_pc_settings.disable_resetti;

    if (Switch(T("Autosave", "자동 저장"), &g_pc_settings.autosave,
               T("Saves by itself when you go in or out of a building, every minute outside and when you quit",
                 "건물에 드나들 때, 밖에서는 1분마다, 게임을 끌 때 저절로 저장합니다"))) {
        save();
    }
    if (Switch(T("Mr. Resetti", "도루묵씨(리셋씨) 등장"), &resetti,
               T("He comes when the game was turned off without saving (off by default: there is the autosave)",
                 "저장하지 않고 끄면 찾아옵니다(자동 저장이 있어 기본은 꺼짐)"))) {
        g_pc_settings.disable_resetti = !resetti;
        save();
    }
    if (Switch(T("Minimap", "미니맵"), &g_pc_settings.minimap,
               T("The town map in the corner outside; M opens the full map",
                 "마을 밖에서 구석에 작은 지도를 띄웁니다. M 키로 전체 지도"))) {
        save();
    }
    if (Switch(T("Seamless acres", "구역 이어 걷기"), &g_pc_settings.seamless_blocks,
               T("Outside, you walk on into the next acre and the camera follows you, with no stop at its edge "
                 "(off: as on the GameCube)",
                 "마을 밖에서 다음 구역으로 멈추지 않고 걸어가고 카메라가 따라옵니다(끄면 게임큐브처럼 경계에서 "
                 "잠깐 멈춤)"))) {
        save();
    }
    {
        const char* names[] = { T("Stretch", "늘이기"), "4:3" };
        row(T("NES games", "패미컴 화면 비율"));
        if (ImGui::Combo("##nes", &g_pc_settings.nes_aspect, names, 2)) {
            save();
        }
    }
    if (Switch(T("On-screen controls", "화면 터치 패드"), &g_pc_settings.touch_controls,
               T("A GameCube pad drawn over the game for touch screens", "터치 화면용 게임큐브 패드를 그립니다"))) {
        save();
    }
    {
        const char* names[] = { T("Built-in Hangul (dubeolsik)", "내장 두벌식"), T("System input method", "시스템 입력기") };
        row(T("Typing", "글자 입력 방식"));
        if (ImGui::Combo("##ime", &g_pc_settings.text_input, names, 2)) {
            save();
        }
    }
#if defined(PC_UPDATER) || defined(__ANDROID__)
    if (Switch(T("Check for updates at start", "시작할 때 업데이트 확인"), &g_pc_settings.auto_update)) {
        save();
    }
#endif
#if defined(PC_UPDATER) && !defined(__ANDROID__)
    {
        char buf[64];

        snprintf(buf, sizeof(buf), "%s (v%s)", T("Check now", "지금 확인"), pc_update_version());
        row(T("Version", "버전"));
        if (SmallButton(buf)) {
            pc_update_check_now();
            s_update_open = true;
            s_update_resume = false;
        }
    }
#endif
    row(T("Town data", "마을 데이터"),
        T("Deletes a town from the memory card to start a new game. The autosave keeps every town, so this is the way "
          "to start over.",
          "메모리 카드에서 마을을 지우고 새 게임을 시작합니다. 자동 저장이 마을을 늘 저장해 두므로, 처음부터 다시 하려면 "
          "이것을 씁니다."));
    if (SmallButton(T("Delete town...", "마을 데이터 초기화..."))) {
        for (int chan = 0; chan < 2; chan++) {
            s_town_have[chan] = pc_autosave_town_info(chan, s_town_name[chan], sizeof(s_town_name[chan]),
                                                      s_town_players[chan], sizeof(s_town_players[chan])) != 0;
        }
        s_town_chan = -1;
        s_town_open = true;
    }
}

static void tab_controls() {
    char buf[64];
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;

    Help(T("Click a cell (or press Enter on it), then press the key or button. Esc cancels.",
           "칸을 누른 뒤(또는 Enter) 바꿀 키나 버튼을 누르세요. Esc는 취소."));
    if (ImGui::BeginTable("##binds", 3, flags)) {
        ImGui::TableSetupColumn(T("Button", "버튼"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(T("Keyboard", "키보드"), ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn(T("Gamepad", "게임패드"), ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableHeadersRow();
        for (int a = 0; a < PC_BIND_COUNT; a++) {
            ImGui::PushID(a);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(a == PC_BIND_MAP    ? T("Map", "지도")
                                   : a == PC_BIND_CHAT ? T("Chat (multiplayer)", "채팅 (멀티플레이)")
                                                       : pc_keybindings_action_name(a));
            for (int col = 0; col < 2; col++) {
                ImGui::TableNextColumn();
                bool waiting = s_capture == a && s_capture_col == col;

                if (waiting) {
                    Uint32 now = SDL_GetTicks();
                    int secs = (int)((s_capture_deadline > now ? s_capture_deadline - now : 0) + 999) / 1000;

                    snprintf(buf, sizeof(buf), T("Press... %d###c%d", "누르세요... %d###c%d"), secs, col);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.98f, 0.80f, 0.45f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.80f, 0.45f, 1.0f));
                } else {
                    char label[48];

                    if (col == 0) {
                        pc_keybindings_key_label(a, label, sizeof(label));
                    } else {
                        pc_keybindings_pad_label(a, label, sizeof(label));
                        if (strcmp(label, "-") == 0 && a >= PC_BIND_STICK_UP && a <= PC_BIND_CSTICK_RIGHT) {
                            snprintf(label, sizeof(label), "%s", a <= PC_BIND_STICK_RIGHT ? "(L stick)" : "(R stick)");
                        }
                    }
                    snprintf(buf, sizeof(buf), "%s###c%d", label, col);
                }
                if (ImGui::Button(buf, ImVec2(-FLT_MIN, 0)) && !waiting) {
                    s_capture = a;
                    s_capture_col = col;
                    s_capture_deadline = SDL_GetTicks() + CAPTURE_MS;
                }
                if (waiting) {
                    ImGui::PopStyleColor(2);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (SmallButton(T("Reset to defaults", "기본값으로"))) {
        pc_keybindings_reset();
        pc_keybindings_save();
    }
    if (s_capture >= 0 && SDL_TICKS_PASSED(SDL_GetTicks(), s_capture_deadline)) {
        s_capture = -1;
    }
}

static void tab_disc() {
    char buf[512];
    int installed = pc_ko_patch_installed();

    Heading(T("Disc image", "디스크 이미지"));
    pc_rom_dir_describe(buf, sizeof(buf));
    row(T("Folder", "폴더"));
    ImGui::TextWrapped("%s", buf[0] ? buf : T("(rom folder next to the game)", "(게임 옆 rom 폴더)"));
    if (pc_rom_dir_can_pick()) {
        if (SmallButton(T("Choose folder...", "폴더 선택..."))) {
            pc_rom_dir_pick();
        }
    } else {
        Help(T("Put the image in the rom folder, or set rom_dir in settings.ini.",
               "디스크 이미지를 rom 폴더에 넣거나 settings.ini의 rom_dir을 정하세요."));
    }

    Heading(T("Korean patch", "한글패치"));
    if (installed) {
        int on = g_pc_settings.korean;

        if (Switch(T("Play in Korean", "한국어로 플레이"), &on,
                   T("Switches between the Korean disc and the original from the next start",
                     "다음 시작부터 한국어판과 원래 디스크를 오갑니다"))) {
            g_pc_settings.korean = on;
            save();
        }
    } else if (g_pc_ko_mode) {
        ImGui::TextColored(Vec(ColGood()), "%s", T("On (the disc is patched)", "켜짐 (패치된 디스크)"));
    } else {
        ImGui::TextDisabled("%s", T("Not installed", "설치되지 않음"));
    }
    if (SmallButton(installed ? T("Install again...", "다시 설치...") : T("Install...", "설치..."))) {
        /* picks the file (blocks while the picker is open), then runs on a thread */
        if (pc_ko_patch_start()) {
            s_patch_open = true;
        }
    }
    Help(pc_rom_dir_can_pick() ? T("Choose the downloaded patch zip as it is (or the .xdelta in it).",
                                   "받은 한글패치 zip을 그대로(또는 안의 .xdelta) 고르세요.")
                               : T("Put the patch zip in the patch folder next to the game first.",
                                   "먼저 게임 옆 patch 폴더에 한글패치 zip을 넣으세요."));
    Help(T("The Korean patch is a separate fan work by 역전맞고 (retrodb.info).",
           "한글패치는 이 프로젝트와 별개인 역전맞고 님의 팬 패치입니다 (retrodb.info)."));
}

static void tab_interface() {
    const char* langs[] = { T("Automatic", "자동"), "한국어", "English" };

    row(T("Language", "언어"));
    if (ImGui::Combo("##lang", &g_pc_settings.ui_language, langs, 3)) {
        save();
    }
    int auto_size = g_pc_settings.ui_scale == 0;
    if (Switch(T("Automatic menu size", "메뉴 크기 자동"), &auto_size)) {
        g_pc_settings.ui_scale = auto_size ? 0 : 100;
        save();
    }
    if (!auto_size) {
        static int pending = 0;

        if (!ImGui::IsAnyItemActive()) {
            pending = g_pc_settings.ui_scale;
        }
        row(T("Menu size", "메뉴 크기"));
        ImGui::SliderInt("##uiscale", &pending, 50, 250, "%d%%");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            g_pc_settings.ui_scale = pending; /* applied once let go: the slider moves with the menu */
            save();
        }
    }
}

static void tab_about() {
    char buf[96];

    snprintf(buf, sizeof(buf), "afe-port v%s", pc_update_version());
    ImGui::TextUnformatted(buf);
    Link("github.com/cailet0422/afe-port", "https://github.com/cailet0422/afe-port");
    Heading(T("Credits", "만든 사람들"));
    Help(T("Game code: afe-decomp (CC0). Platform layer based on ACGC-PC-Port (MIT). NES games: FixNES (MIT).",
           "게임 코드: afe-decomp (CC0). 플랫폼 계층: ACGC-PC-Port (MIT) 기반. 패미컴: FixNES (MIT)."));
    Help(T("Menus: Dear ImGui (MIT). Menu font: NanumGothic by NHN / Sandoll (SIL OFL 1.1), as \"AFE Menu Gothic\".",
           "메뉴: Dear ImGui (MIT). 메뉴 글꼴: 나눔고딕 (NHN·산돌, SIL OFL 1.1, \"AFE Menu Gothic\"으로 부분 수록)."));
    Help(T("HD textures: BC7 decoding by bcdec (MIT), PNG by stb_image (public domain).",
           "HD 텍스처: BC7 해제 bcdec (MIT), PNG stb_image (퍼블릭 도메인)."));
    Help(T("Korean patch: a separate fan work by 역전맞고. HD texture pack: see the HD textures tab.",
           "한글패치: 역전맞고 님의 별도 팬 작업. HD 텍스처팩: [HD 텍스처] 탭 참고."));
    Help(T("Animal Crossing (Doubutsu no Mori) is a trademark of Nintendo. This is an unofficial fan project.",
           "「동물의 숲」은 Nintendo의 상표입니다. 이 프로젝트는 비공식 팬 프로젝트입니다."));
}

static void draw_settings() {
    if (!begin_page("##settings", 820, 600, false)) {
        ImGui::End();
        return;
    }
    if (header(T("Settings", "설정"))) {
        s_page = PAGE_MAIN;
        s_focus_first = true;
    }
    if (restart_needed()) {
        ImGui::TextColored(Vec(ColWarn()), "%s", T("Restart the game to apply all changes", "모든 변경을 적용하려면 게임을 다시 시작하세요"));
    }

    static const char* const names_en[TAB_COUNT] = { "Video", "Audio", "Gameplay", "Controls",
                                                     "Disc", "HD textures", "Interface", "About" };
    static const char* const names_ko[TAB_COUNT] = { "화면", "소리", "게임", "조작", "디스크·한글", "HD 텍스처", "인터페이스", "정보" };

    s_label_w = ImGui::GetContentRegionAvail().x * 0.42f;
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip)) {
        for (int t = 0; t < TAB_COUNT; t++) {
            char label[64];
            ImGuiTabItemFlags f = 0;

            snprintf(label, sizeof(label), "%s###tab%d", Korean() ? names_ko[t] : names_en[t], t);
            if (s_tab_request == t) {
                f |= ImGuiTabItemFlags_SetSelected;
            }
            if (ImGui::BeginTabItem(label, NULL, f)) {
                s_tab = t;
                ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
                TouchScroll();
                ImGui::Spacing();
                switch (t) {
                    case TAB_VIDEO: tab_video(); break;
                    case TAB_AUDIO: tab_audio(); break;
                    case TAB_GAMEPLAY: tab_gameplay(); break;
                    case TAB_CONTROLS: tab_controls(); break;
                    case TAB_DISC: tab_disc(); break;
                    case TAB_TEXTURES: TexturePackTab(); break;
                    case TAB_INTERFACE: tab_interface(); break;
                    case TAB_ABOUT: tab_about(); break;
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        s_tab_request = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

/* --- Popups --------------------------------------------------------------- */

static bool begin_modal(const char* id, bool open) {
    if (open && !ImGui::IsPopupOpen(id)) {
        ImGui::OpenPopup(id);
    }
    if (!open) {
        return false;
    }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float w = 520.0f * g_scale;

    if (w > vp->Size.x * 0.92f) w = vp->Size.x * 0.92f;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, vp->Size.y * 0.9f));
    return ImGui::BeginPopupModal(id, NULL,
                                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
}

/* Up to three buttons side by side; returns the one pressed or -1 */
static int buttons(const char* a, const char* b = nullptr, const char* c = nullptr, int primary = 0) {
    const char* names[3] = { a, b, c };
    int n = c ? 3 : b ? 2 : 1;
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float w = (ImGui::GetContentRegionAvail().x - gap * (n - 1)) / n;
    int pressed = -1;

    for (int i = 0; i < n; i++) {
        if (i > 0) ImGui::SameLine();
        if (BigButton(names[i], w, i == primary)) pressed = i;
    }
    return pressed;
}

static void centered_text(const char* text, ImU32 color = 0) {
    ImGui::PushTextWrapPos(0.0f);
    float w = ImGui::CalcTextSize(text, NULL, false, ImGui::GetContentRegionAvail().x).x;
    CenterNext(w);
    if (color != 0) ImGui::PushStyleColor(ImGuiCol_Text, Vec(color));
    ImGui::TextUnformatted(text);
    if (color != 0) ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
}

static void popup_quit() {
    if (!begin_modal("##quit", s_quit_open)) {
        return;
    }
    title(T("Quit the game?", "게임을 종료할까요?"));
    centered_text(T("Progress since the last save is lost.", "마지막으로 저장한 뒤의 진행은 사라집니다."), ColMuted());
    ImGui::Spacing();
    int b = buttons(T("Cancel", "취소"), T("Quit", "종료"));
    if (b == 0) {
        s_quit_open = false;
        ImGui::CloseCurrentPopup();
    } else if (b == 1) {
        printf("[PAUSE] Quit confirmed\n");
        pc_autosave_quit();
        g_pc_running = 0;
    }
    ImGui::EndPopup();
}

static void popup_town() {
    char buf[320];

    if (!begin_modal("##town", s_town_open)) {
        return;
    }
    if (s_town_chan < 0) {
        int n = 0;

        title(T("Delete a town", "마을 데이터 초기화"));
        for (int chan = 0; chan < 2; chan++) {
            if (!s_town_have[chan]) continue;
            n++;
            snprintf(buf, sizeof(buf), T("Card %c: %s (%s)", "카드 %c: %s 마을 (%s)"), 'A' + chan,
                     s_town_name[chan][0] ? s_town_name[chan] : "?",
                     s_town_players[chan][0] ? s_town_players[chan] : "-");
            centered_text(buf);
            ImGui::PushID(chan);
            if (buttons(T("Delete this town", "이 마을 지우기")) == 0) {
                s_town_chan = chan;
            }
            ImGui::PopID();
            ImGui::Spacing();
        }
        if (n == 0) {
            centered_text(T("There is no town on the memory cards. A new game starts from the title screen.",
                            "메모리 카드에 마을이 없습니다. 타이틀 화면에서 새 게임을 시작하면 됩니다."),
                          ColMuted());
            ImGui::Spacing();
        }
        if (buttons(T("Cancel", "취소")) == 0) {
            s_town_open = false;
            ImGui::CloseCurrentPopup();
        }
    } else {
        int chan = s_town_chan;

        if (pc_gui_korean()) {
            snprintf(buf, sizeof(buf), "카드 %c의 %s 마을을 지울까요?", 'A' + chan,
                     s_town_name[chan][0] ? s_town_name[chan] : "?");
        } else {
            snprintf(buf, sizeof(buf), "Delete %s on card %c?", s_town_name[chan][0] ? s_town_name[chan] : "the town",
                     'A' + chan);
        }
        title(buf);
        centered_text(T("Its players, houses, villagers and everything in it are deleted. This cannot be undone.",
                        "플레이어, 집, 주민, 마을의 모든 것이 지워지며 되돌릴 수 없습니다."),
                      ColWarn());
        centered_text(T("The game then restarts; start a new game from the title screen.",
                        "지우면 게임이 다시 시작됩니다. 타이틀 화면에서 새 게임을 시작하세요."),
                      ColMuted());
        ImGui::Spacing();
        int b = buttons(T("Cancel", "취소"), T("Delete and restart", "지우고 다시 시작"));
        if (b == 0) {
            s_town_chan = -1;
        } else if (b == 1) {
            printf("[PAUSE] delete the town on card %c\n", 'A' + chan);
            if (pc_autosave_delete_town(chan)) {
                pc_request_restart();
            } else {
                pc_gui_notify(T("Could not delete the town file", "마을 파일을 지우지 못했습니다"));
                s_town_open = false;
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::EndPopup();
}

static void popup_resolution() {
    if (s_res_open && SDL_TICKS_PASSED(SDL_GetTicks(), s_res_deadline)) {
        res_revert();
    }
    if (!begin_modal("##res", s_res_open)) {
        return;
    }
    char buf[96];
    Uint32 now = SDL_GetTicks();
    int secs = (int)((s_res_deadline > now ? s_res_deadline - now : 0) + 999) / 1000;

    title(T("Keep this display setting?", "이 화면 설정을 유지할까요?"));
    snprintf(buf, sizeof(buf), T("Going back in %d s", "%d초 뒤에 되돌립니다"), secs);
    centered_text(buf, ColWarn());
    ImGui::Spacing();
    int b = buttons(T("Keep", "유지"), T("Revert", "되돌리기"));
    if (b == 0) {
        res_keep();
        ImGui::CloseCurrentPopup();
    } else if (b == 1) {
        res_revert();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

static void popup_patch() {
    if (s_patch_open) {
        pc_ko_patch_poll();
    }
    if (!begin_modal("##kopatch", s_patch_open)) {
        return;
    }
    PCKoPatchState st = pc_ko_patch_state();

    title(T("Korean patch", "한글패치"));
    if (st == PC_KO_PATCH_RUNNING) {
        centered_text(pc_ko_patch_message());
        Progress(pc_ko_patch_progress());
        if (buttons(T("Cancel", "취소")) == 0) {
            pc_ko_patch_cancel();
        }
    } else if (st == PC_KO_PATCH_DONE) {
        centered_text(T("Installed. It runs from the next start.", "설치했습니다. 다음 시작부터 한국어로 실행됩니다."), ColGood());
        centered_text(pc_ko_patch_detail(), ColMuted());
        centered_text(T("Progress since the last save is lost.", "마지막 저장 뒤의 진행은 사라집니다."), ColMuted());
        int b = buttons(T("Restart now", "지금 다시 시작"), T("Later", "나중에"));
        if (b == 0) {
            pc_request_restart();
        } else if (b == 1) {
            pc_ko_patch_reset();
            s_patch_open = false;
            ImGui::CloseCurrentPopup();
        }
    } else {
        centered_text(pc_ko_patch_message(), ColWarn());
        centered_text(pc_ko_patch_detail());
        if (buttons("OK") == 0) {
            pc_ko_patch_reset();
            s_patch_open = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

static void popup_update() {
    if (!begin_modal("##update", s_update_open)) {
        return;
    }
    PCUpdateState st = pc_update_state();
    char buf[128];

    title(T("Update", "업데이트"));
    switch (st) {
        case PC_UPDATE_AVAILABLE: {
            snprintf(buf, sizeof(buf), T("Version %s is out (you have v%s)", "새 버전 %s이(가) 나왔습니다 (지금 v%s)"),
                     pc_update_new_version(), pc_update_version());
            centered_text(buf, ColGood());
            centered_text(T("Saves, settings and the disc stay as they are.", "세이브, 설정, 디스크는 그대로 남습니다."), ColMuted());
            int b = buttons(T("Update now", "지금 업데이트"), T("Later", "나중에"), T("Skip this version", "이 버전 건너뛰기"));
            if (b == 0) {
                pc_update_start_download();
            } else if (b == 1) {
                ImGui::CloseCurrentPopup();
                update_close();
            } else if (b == 2) {
                pc_update_skip();
                ImGui::CloseCurrentPopup();
                update_close();
            }
            break;
        }
        case PC_UPDATE_DOWNLOADING:
            centered_text(pc_update_message());
            Progress(pc_update_progress());
            if (buttons(T("Cancel", "취소")) == 0) {
                pc_update_cancel();
            }
            break;
        case PC_UPDATE_INSTALLED: {
            centered_text(pc_update_message(), ColGood());
            centered_text(T("It runs from the next start. Progress since the last save is lost.",
                            "다음 시작부터 새 버전입니다. 마지막 저장 뒤의 진행은 사라집니다."),
                          ColMuted());
            int b = buttons(T("Restart now", "지금 다시 시작"), T("Later", "나중에"));
            if (b == 0) {
                pc_request_restart(); /* the new program (pc_update_restart_path) */
            } else if (b == 1) {
                ImGui::CloseCurrentPopup();
                update_close();
            }
            break;
        }
        case PC_UPDATE_CHECKING:
            centered_text(pc_update_message());
            break;
        case PC_UPDATE_FAILED:
            centered_text(pc_update_message(), ColWarn());
            Link("github.com/cailet0422/afe-port/releases", "https://github.com/cailet0422/afe-port/releases");
            if (buttons("OK") == 0) {
                ImGui::CloseCurrentPopup();
                update_close();
            }
            break;
        default:
            centered_text(pc_update_message());
            if (buttons("OK") == 0) {
                ImGui::CloseCurrentPopup();
                update_close();
            }
            break;
    }
    ImGui::EndPopup();
}

/* --- Frame ---------------------------------------------------------------- */

void MenuDraw() {
    ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                                  IM_COL32(0x10, 0x0C, 0x06, 0x90));
    switch (s_page) {
        case PAGE_MAIN:
            draw_main();
            break;
        case PAGE_SETTINGS:
            draw_settings();
            break;
        case PAGE_MULTIPLAYER: {
            bool back = false;

            MultiplayerPage(&back);
            if (back) {
                s_page = PAGE_MAIN;
                s_focus_first = true;
            }
            break;
        }
    }
    popup_quit();
    popup_town();
    popup_resolution();
    popup_patch();
    popup_update();
    TexturePackPopups();
    MultiplayerPopups();
}

} // namespace gui

/* --- The old settings menu's C API, still asked by the game ---------------- */

extern "C" int pc_settings_menu_active(void) {
    return g_pc_paused != 0;
}
