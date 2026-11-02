/* pc_gui_mp.cpp - the multiplayer page and the name tags (pc_mp_game.h) */
#include "pc_gui_internal.h"

extern "C" {
#include "pc_platform.h"
#include "pc_mp.h"
#include "pc_mp_game.h"
#include "pc_settings.h"
#include "pc_typing.h"
#include "pc_keybindings.h"
}

#include <string.h>

namespace gui {

enum { MP_BROWSE, MP_HOST, MP_CODE };

static int s_tab = MP_BROWSE;
static int s_tab_request = -1;
static bool s_opened = false;

static char s_room_name[48] = "";
static char s_host_pass[32] = "";
static int s_public = 1;
static int s_max_players = 4;

static char s_code[64] = "";
static char s_code_pass[32] = "";
static char s_error[160] = "";

static bool s_pass_open = false;
static PcMpRoomInfo s_pass_room;
static char s_pass[32] = "";

static int s_kick_slot = -1;
static char s_kick_name[PC_MP_NAME_LEN] = "";
static bool s_leave_open = false;

static char s_chat_line[160] = "";

static float row_h() {
    return ImGui::GetFrameHeight();
}

static void join_room(const PcMpRoomInfo* r) {
    if (r->has_password) {
        s_pass_room = *r;
        s_pass[0] = '\0';
        s_pass_open = true;
    } else {
        pc_mp_game_join_room(r, "");
    }
}

static void browse_tab(PcMp* mp) {
    PcMpRoomInfo rooms[PC_MP_MAX_ROOMS];
    int n = pc_mp_rooms(mp, rooms, PC_MP_MAX_ROOMS);
    char buf[256];

    pc_mp_game_browse(1);
    Help(T("Public rooms on this network and on the internet (found through the BitTorrent DHT, no server).",
           "같은 네트워크와 인터넷(BitTorrent DHT, 서버 없음)에서 찾은 공개방입니다."));
    if (n == 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", T("Looking for rooms...", "방을 찾는 중..."));
    }
    for (int i = 0; i < n; i++) {
        const PcMpRoomInfo* r = &rooms[i];

        ImGui::PushID((int)r->room_id);
        ImGui::BeginGroup();
        snprintf(buf, sizeof(buf), "%s%s", r->name, r->has_password ? T("  (password)", "  (비밀번호)") : "");
        ImGui::TextUnformatted(buf);
        snprintf(buf, sizeof(buf), T("%s's town %s - %d/%d players - %s%s", "%s의 %s 마을 - %d/%d명 - %s%s"), r->host_name,
                 r->town, r->players, r->max_players, r->lan ? T("this network", "같은 네트워크") : T("internet", "인터넷"),
                 r->korean ? T(", Korean", ", 한국어판") : "");
        Help(buf);
        ImGui::EndGroup();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 110.0f * g_scale);
        if (r->protocol != PC_MP_PROTOCOL) {
            ImGui::TextDisabled("%s", T("other version", "다른 버전"));
        } else if (r->players >= r->max_players) {
            ImGui::TextDisabled("%s", T("full", "가득 참"));
        } else if (SmallButton(T("Join", "참여"))) {
            join_room(r);
        }
        ImGui::Separator();
        ImGui::PopID();
    }
}

static void host_tab() {
    char name[PC_MP_NAME_LEN], town[32];

    pc_mp_game_my_names(name, sizeof(name), town, sizeof(town));
    if (s_room_name[0] == '\0') {
        snprintf(s_room_name, sizeof(s_room_name), pc_gui_korean() ? "%s 마을" : "%s's town", town[0] ? town : name);
    }
    Help(T("Friends visit your town: they take the train from their own station, and you all see each other, "
           "drop, pick up, plant and dig together. Save first: guests get your town as last saved plus what you do.",
           "친구들이 내 마을로 놀러옵니다. 손님은 자기 마을 기차역에서 기차를 타고 오고, 함께 보고 아이템을 떨어뜨리고 "
           "줍고 심고 팔 수 있어요. 먼저 한 번 저장해 두세요."));
    RowLabel(T("Room name", "방 이름"));
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##rname", s_room_name, sizeof(s_room_name));
    Switch(T("Public (listed for everyone)", "공개방 (누구나 찾을 수 있음)"), &s_public,
           T("Private rooms are joined with the room code only", "비공개방은 방 코드로만 들어올 수 있어요"));
    RowLabel(T("Password (optional)", "비밀번호 (선택)"));
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##rpass", s_host_pass, sizeof(s_host_pass), ImGuiInputTextFlags_Password);
    RowLabel(T("Players", "최대 인원"));
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##rmax", &s_max_players, 2, PC_MP_MAX_PLAYERS);
    ImGui::Spacing();
    if (BigButton(T("Open the room", "방 열기"), -1, true)) {
        PcMpHostConfig cfg;

        memset(&cfg, 0, sizeof(cfg));
        snprintf(cfg.room_name, sizeof(cfg.room_name), "%s", s_room_name);
        snprintf(cfg.password, sizeof(cfg.password), "%s", s_host_pass);
        snprintf(cfg.town, sizeof(cfg.town), "%s", town);
        cfg.is_public = s_public;
        cfg.max_players = s_max_players;
        if (!pc_mp_game_host(&cfg)) {
            snprintf(s_error, sizeof(s_error), "%s", T("Could not open the room", "방을 열지 못했습니다"));
        }
    }
}

static void code_tab() {
    Help(T("Enter the room code the host gave you (or an address like 1.2.3.4:47070).",
           "호스트가 알려 준 방 코드(또는 1.2.3.4:47070 같은 주소)를 넣으세요."));
    RowLabel(T("Room code", "방 코드"));
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##code", s_code, sizeof(s_code), ImGuiInputTextFlags_CharsUppercase);
    RowLabel(T("Password", "비밀번호"));
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##cpass", s_code_pass, sizeof(s_code_pass), ImGuiInputTextFlags_Password);
    if (SmallButton(T("Paste", "붙여넣기"))) {
        char* clip = SDL_GetClipboardText();

        if (clip != NULL) {
            snprintf(s_code, sizeof(s_code), "%s", clip);
            SDL_free(clip);
        }
    }
    ImGui::Spacing();
    if (BigButton(T("Join", "참여"), -1, true)) {
        if (!pc_mp_game_join_text(s_code, s_code_pass)) {
            snprintf(s_error, sizeof(s_error), "%s", T("That is not a room code or an address", "방 코드나 주소가 아닙니다"));
        }
    }
}

static const char* visit_text(int* progress) {
    switch (pc_mp_game_visit(progress)) {
        case PC_MP_VISIT_DOWNLOADING:
            return T("Receiving the host's town...", "호스트 마을을 받는 중...");
        case PC_MP_VISIT_READY:
            return T("Ready! Go to your train station and ask the station master for a trip to the host's town.",
                     "준비 완료! 내 마을 기차역 역무원에게 말을 걸어 호스트 마을로 여행을 떠나세요.");
        case PC_MP_VISIT_THERE:
            return T("You are in the host's town. Come back home by train as usual.",
                     "호스트 마을에 있습니다. 돌아갈 때는 평소처럼 기차를 타세요.");
        case PC_MP_VISIT_FAILED:
            return T("Could not receive the host's town", "호스트 마을을 받지 못했습니다");
        default:
            return NULL;
    }
}

static void room_view(PcMp* mp) {
    const PcMpRoomInfo* room = pc_mp_room(mp);
    bool host = pc_mp_mode(mp) == PC_MP_HOSTING;
    PcMpPlayer players[PC_MP_MAX_PLAYERS];
    int n = pc_mp_players(mp, players, PC_MP_MAX_PLAYERS);
    char buf[256];

    Heading(room->name);
    if (host) {
        char code[32];

        pc_mp_room_code(mp, code, sizeof(code));
        snprintf(buf, sizeof(buf), "%s  %s", T("Room code:", "방 코드:"), code);
        ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * 1.25f);
        ImGui::TextUnformatted(buf);
        ImGui::PopFont();
        ImGui::SameLine();
        if (SmallButton(T("Copy", "복사"))) {
            SDL_SetClipboardText(code);
            pc_gui_notify(pc_gui_korean() ? "방 코드를 복사했습니다" : "Room code copied");
        }
        Help(room->is_public ? T("Public: listed on this network and the internet. Friends can also use the code.",
                                 "공개방: 같은 네트워크와 인터넷 목록에 보입니다. 코드로도 들어올 수 있어요.")
                             : T("Private: only who has the code (and the password) gets in.",
                                 "비공개방: 코드(와 비밀번호)를 아는 사람만 들어옵니다."));
    } else {
        int progress = 0;
        const char* v = visit_text(&progress);

        if (v != NULL) {
            ImGui::TextColored(Vec(ColGood()), "%s", "");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(v);
            ImGui::PopTextWrapPos();
            if (pc_mp_game_visit(NULL) == PC_MP_VISIT_DOWNLOADING && progress >= 0) Progress(progress);
        }
    }
    pc_mp_game_net_status(buf, sizeof(buf));
    Help(buf);

    snprintf(buf, sizeof(buf), T("Players (%d/%d)", "참가자 (%d/%d)"), n, room->max_players);
    Heading(buf);
    for (int i = 0; i < n; i++) {
        const PcMpPlayer* p = &players[i];

        ImGui::PushID(p->slot);
        ImGui::AlignTextToFramePadding();
        snprintf(buf, sizeof(buf), "%s%s%s", p->name, p->slot == 0 ? T("  (host)", "  (호스트)") : "",
                 p->is_you ? T("  - you", "  - 나") : "");
        ImGui::TextUnformatted(buf);
        if (!p->is_you && p->ping_ms > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%d ms", p->ping_ms);
        }
        if (host && !p->is_you) {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 120.0f * g_scale);
            if (SmallButton(T("Send out", "내보내기"))) {
                s_kick_slot = p->slot;
                snprintf(s_kick_name, sizeof(s_kick_name), "%s", p->name);
            }
        }
        ImGui::PopID();
    }

    Heading(T("Chat", "채팅"));
    {
        char lines[8][192];
        int k = pc_mp_game_chat_lines(lines, 8);

        for (int i = 0; i < k; i++) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(lines[i]);
            ImGui::PopTextWrapPos();
        }
        float send_w = 90.0f * g_scale;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - send_w - ImGui::GetStyle().ItemSpacing.x);
        bool enter = ImGui::InputText("##chat", s_chat_line, sizeof(s_chat_line), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((SmallButton(T("Send", "보내기")) || enter) && s_chat_line[0] != '\0') {
            pc_mp_game_chat(s_chat_line);
            s_chat_line[0] = '\0';
            if (enter) ImGui::SetKeyboardFocusHere(-1);
        }
        char key[32], help[200];

        pc_keybindings_key_label(PC_BIND_CHAT, key, sizeof(key));
        if (strcmp(key, "-") != 0) {
            snprintf(help, sizeof(help),
                     T("While playing: the %s key (Settings > Controls > Chat) or, on a touch screen, the chat button.",
                       "게임 중에는 %s 키(설정 > 조작 > 채팅)나 터치 화면의 채팅 버튼으로 바로 칠 수 있어요."),
                     key);
        } else {
            snprintf(help, sizeof(help), "%s",
                     T("While playing: the chat button on a touch screen, or bind a key to Chat in Settings > Controls.",
                       "게임 중에는 터치 화면의 채팅 버튼으로 칠 수 있어요. 설정 > 조작 > 채팅에 키를 정할 수도 있어요."));
        }
        Help(help);
    }
    ImGui::Spacing();
    if (BigButton(host ? T("Close the room", "방 닫기") : T("Leave the room", "방 나가기"))) {
        s_leave_open = true;
    }
}

void MultiplayerPage(bool* back) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float w = 760.0f * g_scale, h = 620.0f * g_scale;
    PcMp* mp;

    if (!s_opened) {
        pc_mp_game_open();
        s_opened = true;
    }
    mp = pc_mp_game_session();
    if (w > vp->Size.x * 0.94f) w = vp->Size.x * 0.94f;
    if (h > vp->Size.y * 0.92f) h = vp->Size.y * 0.92f;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    if (!ImGui::Begin("##mp", NULL,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (SmallButton(T("< Back", "< 뒤로"))) {
        *back = true;
        pc_mp_game_browse(0);
    }
    ImGui::SameLine();
    ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * 1.3f);
    {
        const char* t = T("Multiplayer", "멀티플레이");
        ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x + ImGui::GetStyle().WindowPadding.x -
                              ImGui::CalcTextSize(t).x) * 0.5f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(Vec(IM_COL32(0x2E, 0x7D, 0x3E, 0xFF)), "%s", t);
    }
    ImGui::PopFont();
    ImGui::Separator();

    if (mp == NULL) {
        ImGui::TextColored(Vec(ColWarn()), "%s",
                           T("The network is not available (no UDP socket).", "네트워크를 쓸 수 없습니다 (UDP 소켓 없음)."));
        ImGui::End();
        return;
    }
    if (s_error[0] != '\0') {
        ImGui::TextColored(Vec(ColWarn()), "%s", s_error);
    }

    ImGui::BeginChild("##mpbody", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
    TouchScroll();
    switch (pc_mp_mode(mp)) {
        case PC_MP_IDLE: {
            static const char* const en[] = { "Find a room", "Host", "Room code" };
            static const char* const ko[] = { "방 찾기", "방 만들기", "코드로 참여" };

            /* your name */
            RowLabel(T("Your name", "내 이름"));
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.35f);
            ImGui::SetNextItemWidth(-FLT_MIN);
            {
                char hint[PC_MP_NAME_LEN], town[32];

                pc_mp_game_my_names(hint, sizeof(hint), town, sizeof(town));
                if (ImGui::InputTextWithHint("##myname", hint, g_pc_settings.mp_name, sizeof(g_pc_settings.mp_name))) {
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) pc_settings_save();
            }
            if (ImGui::BeginTabBar("##mptabs")) {
                for (int t = 0; t < 3; t++) {
                    char label[64];
                    ImGuiTabItemFlags f = s_tab_request == t ? ImGuiTabItemFlags_SetSelected : 0;

                    snprintf(label, sizeof(label), "%s###mptab%d", Korean() ? ko[t] : en[t], t);
                    if (ImGui::BeginTabItem(label, NULL, f)) {
                        if (s_tab != t) s_error[0] = '\0';
                        s_tab = t;
                        ImGui::Spacing();
                        if (t == MP_BROWSE) browse_tab(mp);
                        else pc_mp_game_browse(0);
                        if (t == MP_HOST) host_tab();
                        if (t == MP_CODE) code_tab();
                        ImGui::EndTabItem();
                    }
                }
                s_tab_request = -1;
                ImGui::EndTabBar();
            }
            break;
        }
        case PC_MP_JOINING:
            ImGui::Spacing();
            ImGui::TextUnformatted(T("Getting in... (over the internet this can take up to half a minute)",
                                     "들어가는 중... (인터넷에서는 30초쯤 걸릴 수 있어요)"));
            if (BigButton(T("Cancel", "취소"))) {
                pc_mp_game_leave();
            }
            break;
        default:
            s_error[0] = '\0';
            room_view(mp);
            break;
    }
    ImGui::EndChild();
    ImGui::End();
}

static bool modal(const char* id, bool open) {
    if (!open) return false;
    if (!ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float w = 480.0f * g_scale;

    if (w > vp->Size.x * 0.92f) w = vp->Size.x * 0.92f;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, vp->Size.y * 0.9f));
    return ImGui::BeginPopupModal(id, NULL,
                                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
}

static int two_buttons(const char* a, const char* b) {
    float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    int r = -1;

    if (BigButton(a, w)) r = 0;
    ImGui::SameLine();
    if (BigButton(b, w, true)) r = 1;
    return r;
}

void MultiplayerPopups() {
    if (modal("##mppass", s_pass_open)) {
        Heading(s_pass_room.name);
        ImGui::TextUnformatted(T("Password", "비밀번호"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("##pw", s_pass, sizeof(s_pass),
                                      ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        int b = two_buttons(T("Cancel", "취소"), T("Join", "참여"));
        if (b == 1 || enter) {
            pc_mp_game_join_room(&s_pass_room, s_pass);
            s_pass_open = false;
            ImGui::CloseCurrentPopup();
        } else if (b == 0) {
            s_pass_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (modal("##mpkick", s_kick_slot >= 0)) {
        char buf[160];

        snprintf(buf, sizeof(buf), T("Send %s out of the room? They cannot come back in.",
                                     "%s 님을 방에서 내보낼까요? 이 방에는 다시 들어올 수 없습니다."),
                 s_kick_name);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(buf);
        ImGui::PopTextWrapPos();
        int b = two_buttons(T("Cancel", "취소"), T("Send out", "내보내기"));
        if (b == 1) pc_mp_game_kick(s_kick_slot);
        if (b >= 0) {
            s_kick_slot = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (modal("##mpleave", s_leave_open)) {
        PcMp* mp = pc_mp_game_session();
        bool host = mp != NULL && pc_mp_mode(mp) == PC_MP_HOSTING;

        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(host ? T("Close the room? Everyone in it leaves.", "방을 닫을까요? 모두 방에서 나가게 됩니다.")
                                    : T("Leave the room? If you are in the host's town, come home by train.",
                                        "방에서 나갈까요? 호스트 마을에 있다면 기차를 타고 집으로 돌아오세요."));
        ImGui::PopTextWrapPos();
        int b = two_buttons(T("Cancel", "취소"), host ? T("Close", "닫기") : T("Leave", "나가기"));
        if (b == 1) pc_mp_game_leave();
        if (b >= 0) {
            s_leave_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

bool MultiplayerBack() {
    if (s_pass_open) {
        s_pass_open = false;
        return true;
    }
    if (s_kick_slot >= 0) {
        s_kick_slot = -1;
        return true;
    }
    if (s_leave_open) {
        s_leave_open = false;
        return true;
    }
    return false;
}

const char* MultiplayerStatus() {
    static char buf[64];
    PcMp* mp = pc_mp_game_session();
    PcMpPlayer players[PC_MP_MAX_PLAYERS];

    if (mp == NULL || (pc_mp_mode(mp) != PC_MP_HOSTING && pc_mp_mode(mp) != PC_MP_JOINED)) return nullptr;
    snprintf(buf, sizeof(buf), T("%d in the room", "%d명 참가 중"), pc_mp_players(mp, players, PC_MP_MAX_PLAYERS));
    return buf;
}

/* --- Chat while playing ------------------------------------------------ */

static bool s_qc_open = false;
static bool s_qc_focus = false;
static char s_qc_line[160] = "";
static ImVec2 s_qc_min(0, 0), s_qc_max(0, 0); /* the box, as last drawn */
static unsigned s_qc_seen = 0;               /* chat lines seen (in the box or as they came) */
static int s_qc_recent = 0;                  /* lines to show at the top for a while */
static Uint32 s_qc_recent_until = 0;
static int s_qc_own = 0;                     /* lines we sent that have not come round yet */
static Uint32 s_qc_unread_until = 0;         /* someone else said something: the button's dot */

#define QC_RECENT_MS 8000

bool ChatIsOpen() {
    return s_qc_open;
}

void ChatOpen() {
    if (!s_qc_open) {
        s_qc_open = true;
        s_qc_focus = true;
        s_qc_min = s_qc_max = ImVec2(0, 0);
        printf("[GUI] chat box opened\n");
    }
}

void ChatClose() {
    if (s_qc_open) {
        s_qc_open = false;
        s_qc_seen = pc_mp_game_chat_serial();
        s_qc_recent = 0;
        s_qc_unread_until = 0; /* read in the box */
        printf("[GUI] chat box closed\n");
    }
}

bool ChatAllowed() {
    return MultiplayerOverlayWanted() && !pc_typing_takes_keys() && !pc_typing_editor_taking_input();
}

bool ChatWindowContains(float x, float y) {
    if (s_qc_max.x <= s_qc_min.x) {
        return true; /* not drawn yet */
    }
    return x >= s_qc_min.x && x <= s_qc_max.x && y >= s_qc_min.y && y <= s_qc_max.y;
}

/* On touch screens (the on-screen pad, or Android): a round button at the
 * top, between the pad's Start and R */
static bool chat_button_wanted() {
#ifdef __ANDROID__
    return true;
#else
    return g_pc_settings.touch_controls != 0;
#endif
}

static void chat_button_geometry(ImVec2* c, float* r) {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x, h = io.DisplaySize.y;
    float u = w < h ? w : h;

    *c = ImVec2(w * 0.5f + 0.27f * u, 0.075f * u);
    *r = 0.05f * u;
}

bool ChatButtonHit(float x, float y) {
    ImVec2 c;
    float r;

    if (!chat_button_wanted()) {
        return false;
    }
    chat_button_geometry(&c, &r);
    r *= 1.3f; /* a finger is not a pointer */
    return (x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= r * r;
}

static void draw_chat_button(ImDrawList* dl, bool unread) {
    ImVec2 c;
    float r;

    chat_button_geometry(&c, &r);
    dl->AddCircleFilled(c, r, IM_COL32(0xFF, 0xFF, 0xFF, 0x70));
    dl->AddCircle(c, r, IM_COL32(0xFF, 0xFF, 0xFF, 0xC0), 0, 2.0f * g_scale);
    /* a speech bubble with three dots */
    float bw = r * 1.15f, bh = r * 0.78f;
    ImVec2 a(c.x - bw * 0.5f, c.y - bh * 0.5f - r * 0.08f), b(c.x + bw * 0.5f, c.y + bh * 0.5f - r * 0.08f);
    ImU32 green = IM_COL32(0x3E, 0x9A, 0x4F, 0xF0);

    dl->AddRectFilled(a, b, green, bh * 0.45f);
    dl->AddTriangleFilled(ImVec2(a.x + bw * 0.22f, b.y - 1.0f), ImVec2(a.x + bw * 0.46f, b.y - 1.0f),
                          ImVec2(a.x + bw * 0.16f, b.y + bh * 0.38f), green);
    for (int i = -1; i <= 1; i++) {
        dl->AddCircleFilled(ImVec2(c.x + i * bw * 0.25f, (a.y + b.y) * 0.5f), bh * 0.09f, IM_COL32(0xFF, 0xFF, 0xFF, 0xFF));
    }
    if (unread) {
        dl->AddCircleFilled(ImVec2(c.x + r * 0.72f, c.y - r * 0.72f), r * 0.22f, IM_COL32(0xE0, 0x4B, 0x3A, 0xFF));
    }
}

/* The lines that just came, for a few seconds at the top (box closed) */
static void draw_recent(ImDrawList* dl) {
    char lines[3][192];
    int n = pc_mp_game_chat_lines(lines, s_qc_recent < 3 ? s_qc_recent : 3);
    ImGuiIO& io = ImGui::GetIO();
    float u = io.DisplaySize.x < io.DisplaySize.y ? io.DisplaySize.x : io.DisplaySize.y;
    float pad = 8.0f * g_scale;
    float wrap = 420.0f * g_scale;
    Uint32 now = SDL_GetTicks();
    Uint32 left = s_qc_recent_until - now;
    int alpha = left < 600 ? (int)(left * 255 / 600) : 255;
    float fs = ImGui::GetFontSize() * 0.9f;
    float h = 0.0f, w = 0.0f;

    for (int i = 0; i < n; i++) {
        ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, wrap, lines[i]);
        h += ts.y + (i > 0 ? pad * 0.4f : 0.0f);
        if (ts.x > w) w = ts.x;
    }
    /* under the pad's top buttons on a touch screen, else in the top left corner */
    ImVec2 p = chat_button_wanted() ? ImVec2((io.DisplaySize.x - w) * 0.5f, 0.15f * u) : ImVec2(14.0f * g_scale + pad, 14.0f * g_scale + pad);
    dl->AddRectFilled(ImVec2(p.x - pad, p.y - pad), ImVec2(p.x + w + pad, p.y + h + pad),
                      IM_COL32(0x2A, 0x24, 0x1C, alpha * 0xB0 / 255), 10.0f * g_scale);
    for (int i = 0; i < n; i++) {
        ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, wrap, lines[i]);
        dl->AddText(ImGui::GetFont(), fs, p, IM_COL32(0xFF, 0xF8, 0xE7, alpha), lines[i], NULL, wrap);
        p.y += ts.y + pad * 0.4f;
    }
}

static void chat_box() {
    ImGuiIO& io = ImGui::GetIO();
    float u = io.DisplaySize.x < io.DisplaySize.y ? io.DisplaySize.x : io.DisplaySize.y;
    float w = 560.0f * g_scale;
    bool touch = chat_button_wanted() || g_touch;

    if (w > io.DisplaySize.x - 24.0f * g_scale) {
        w = io.DisplaySize.x - 24.0f * g_scale;
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, touch ? 0.02f * u : 14.0f * g_scale), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f), ImGuiCond_Always);
    if (s_qc_focus) {
        ImGui::SetNextWindowFocus();
    }
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin("##chatbox", NULL,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);

    ImGui::PushStyleColor(ImGuiCol_Text, Vec(IM_COL32(0x2E, 0x7D, 0x3E, 0xFF)));
    ImGui::TextUnformatted(T("Chat", "채팅"));
    ImGui::PopStyleColor();
    if (!touch) {
        const char* hint = T("Enter: send   Esc: close", "Enter: 보내기   Esc: 닫기");

        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(hint).x);
        ImGui::PushStyleColor(ImGuiCol_Text, Vec(ColMuted()));
        ImGui::TextUnformatted(hint);
        ImGui::PopStyleColor();
    }

    char lines[5][192];
    int n = pc_mp_game_chat_lines(lines, 5);

    for (int i = 0; i < n; i++) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(lines[i]);
        ImGui::PopTextWrapPos();
    }
    if (n == 0) {
        Help(T("Nothing said yet.", "아직 아무 말도 없어요."));
    }

    float send_w = ImGui::CalcTextSize(T("Send", "보내기")).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float close_w = touch ? ImGui::CalcTextSize(T("Close", "닫기")).x + ImGui::GetStyle().FramePadding.x * 2.0f +
                                ImGui::GetStyle().ItemSpacing.x
                          : 0.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - send_w - close_w - ImGui::GetStyle().ItemSpacing.x);
    if (s_qc_focus) {
        ImGui::SetKeyboardFocusHere();
        s_qc_focus = false;
    }
    bool enter = ImGui::InputText("##quickchat", s_qc_line, sizeof(s_qc_line), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    bool send = SmallButton(T("Send", "보내기"));
    bool close = false;
    if (touch) {
        ImGui::SameLine();
        close = SmallButton(T("Close", "닫기"));
    }
    s_qc_min = ImGui::GetWindowPos();
    s_qc_max = ImVec2(s_qc_min.x + ImGui::GetWindowSize().x, s_qc_min.y + ImGui::GetWindowSize().y);
    ImGui::End();

    if (close || enter || send) {
        ChatClose(); /* (an empty Enter closes too) */
    }
    if ((enter || send) && s_qc_line[0] != '\0') {
        /* after closing: the line shows at the top for a moment */
        s_qc_own++;
        pc_mp_game_chat(s_qc_line);
        s_qc_line[0] = '\0';
    }
}

static void QuickChat() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    unsigned serial = pc_mp_game_chat_serial();

    if (s_qc_open) {
        s_qc_seen = serial;
        chat_box();
        return;
    }
    if (serial != s_qc_seen) {
        int added = (int)(serial - s_qc_seen);
        int own = s_qc_own < added ? s_qc_own : added;

        s_qc_recent = s_qc_recent_until != 0 && SDL_TICKS_PASSED(s_qc_recent_until, SDL_GetTicks()) ? s_qc_recent + added : added;
        s_qc_recent_until = SDL_GetTicks() + QC_RECENT_MS;
        if (added > own) {
            s_qc_unread_until = s_qc_recent_until;
        }
        s_qc_own -= own;
        s_qc_seen = serial;
    }
    bool recent = s_qc_recent > 0 && !SDL_TICKS_PASSED(SDL_GetTicks(), s_qc_recent_until);
    if (recent) {
        draw_recent(dl);
    } else {
        s_qc_recent = 0;
    }
    if (chat_button_wanted() && ChatAllowed()) {
        draw_chat_button(dl, s_qc_unread_until != 0 && !SDL_TICKS_PASSED(SDL_GetTicks(), s_qc_unread_until));
    }
}

bool MultiplayerOverlayWanted() {
    PcMp* mp = pc_mp_game_session();

    return mp != NULL && (pc_mp_mode(mp) == PC_MP_HOSTING || pc_mp_mode(mp) == PC_MP_JOINED);
}

void MultiplayerOverlay() {
    PcMpTag tags[PC_MP_MAX_PLAYERS];
    int n = pc_mp_game_tags(tags, PC_MP_MAX_PLAYERS);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float fs = ImGui::GetFontSize() * 0.9f;
    float pad = 5.0f * g_scale;
    ImGuiIO& io = ImGui::GetIO();
    float sx = io.DisplaySize.x / (float)(g_pc_window_w > 0 ? g_pc_window_w : 1);
    float sy = io.DisplaySize.y / (float)(g_pc_window_h > 0 ? g_pc_window_h : 1);

    for (int i = 0; i < n; i++) {
        ImVec2 size = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, tags[i].name);
        float x = tags[i].x * sx - size.x * 0.5f;
        float y = tags[i].y * sy - size.y - pad;

        dl->AddRectFilled(ImVec2(x - pad, y - pad * 0.5f), ImVec2(x + size.x + pad, y + size.y + pad * 0.5f),
                          IM_COL32(0xFF, 0xF8, 0xE7, 0xD8), 8.0f * g_scale);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(x, y), IM_COL32(0x2E, 0x6B, 0x3A, 0xFF), tags[i].name);
        if (tags[i].said[0] != '\0') {
            /* a speech balloon with what they just said in the chat */
            float wrap = 220.0f * g_scale;
            ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, wrap, tags[i].said);
            float bx = tags[i].x * sx - ts.x * 0.5f;
            float by = y - pad * 2.5f - ts.y;
            ImVec2 a(bx - pad * 1.5f, by - pad), b(bx + ts.x + pad * 1.5f, by + ts.y + pad);
            float cx = tags[i].x * sx;

            dl->AddRectFilled(a, b, IM_COL32(0xFF, 0xFF, 0xFF, 0xF0), 12.0f * g_scale);
            dl->AddTriangleFilled(ImVec2(cx - pad, b.y), ImVec2(cx + pad, b.y), ImVec2(cx, b.y + pad * 1.2f),
                                  IM_COL32(0xFF, 0xFF, 0xFF, 0xF0));
            dl->AddText(ImGui::GetFont(), fs, ImVec2(bx, by), IM_COL32(0x4A, 0x3B, 0x2A, 0xFF), tags[i].said, NULL,
                        wrap);
        }
    }
    QuickChat();
}

} // namespace gui
