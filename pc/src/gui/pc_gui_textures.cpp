/* pc_gui_textures.cpp - Settings > HD textures (see pc_texture_pack.h) */
#include "pc_gui_internal.h"

extern "C" {
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_texture_pack.h"
#include "pc_rom_dir.h"
#include "pc_ko.h"
}

#include <string.h>

namespace gui {

/* Where the pack that the game was made for comes from (the user's request:
 * always name the source) */
static const char* const PACK_THREAD =
    "https://forums.dolphin-emu.org/Thread-animal-crossing-hd-texture-pack-version-25-october-11th-2026";
static const char* const PACK_DOWNLOAD = "https://drive.google.com/file/d/1Z8-pMx20xO8q3caiqy4NOrSNVT1veDSk/view";

static bool s_install_open = false;
static char s_remove_name[128] = "";
static bool s_remove_open = false;

static void size_text(long long bytes, char* buf, size_t n) {
    if (bytes >= (1LL << 30)) {
        snprintf(buf, n, "%.1f GB", (double)bytes / (double)(1LL << 30));
    } else {
        snprintf(buf, n, "%lld MB", bytes >> 20);
    }
}

void TexturePackTab() {
    PCTexPackInfo info;
    char buf[256], a[32], b[32];

    pc_texture_pack_info(&info);

    Heading(T("Installed packs", "설치된 텍스처팩"));
    if (info.sources == 0) {
        ImGui::TextDisabled("%s", T("None yet.", "아직 없습니다."));
    }
    for (int i = 0; i < info.sources; i++) {
        const PCTexPackSource* s = &info.source[i];

        ImGui::PushID(i);
        ImGui::BeginGroup();
        ImGui::TextUnformatted(s->title[0] ? s->title : s->name);
        if (s->version[0]) {
            ImGui::TextDisabled("%s", s->version);
        }
        size_text(s->bytes, a, sizeof(a));
        if (s->is_zip) {
            snprintf(buf, sizeof(buf), T("%s - %d textures, %s (read from the zip)", "%s - 텍스처 %d개, %s (zip에서 바로 읽음)"),
                     s->name, s->entries, a);
        } else {
            snprintf(buf, sizeof(buf), T("%s - %d textures (folders)", "%s - 텍스처 %d개 (폴더)"), s->name, s->entries);
        }
        Help(buf);
        ImGui::EndGroup();
        if (s->is_zip) {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(T("Delete", "삭제")).x -
                            ImGui::GetStyle().FramePadding.x * 2 - 4);
            if (SmallButton(T("Delete", "삭제"))) {
                snprintf(s_remove_name, sizeof(s_remove_name), "%s", s->name);
                s_remove_open = true;
            }
        }
        ImGui::Separator();
        ImGui::PopID();
    }

    if (BigButton(T("Install a texture pack (zip)...", "텍스처팩 설치 (zip 선택)..."))) {
        if (pc_texture_pack_install_start()) {
            s_install_open = true;
        }
    }
    if (!pc_pick_file_available()) {
        Help(T("No file picker here: put the zip as it is into the texture_pack folder next to the game, then press "
               "Read again.",
               "파일 선택 창이 없는 환경입니다. 받은 zip을 그대로 게임 옆 texture_pack 폴더에 넣고 [다시 읽기]를 누르세요."));
    }
    if (SmallButton(T("Read the folder again", "다시 읽기"))) {
        pc_texture_pack_reload();
    }

    Heading(T("Use", "사용"));
    int on = g_pc_settings.hd_textures;
    if (Switch(T("HD textures", "HD 텍스처 사용"), &on)) {
        pc_texture_pack_set_enabled(on);
        pc_settings_save();
    }
    {
        const char* names[] = { T("While playing (no wait)", "플레이하면서 (대기 없음)"),
                                T("All at start (needs a lot of memory)", "시작할 때 전부 (메모리 많이 사용)") };
        RowLabel(T("Loading", "불러오기"));
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##preload", &g_pc_settings.preload_textures, names, 2)) {
            pc_settings_save();
        }
    }
    {
        const char* names[] = { T("Full (as in the pack)", "원본 크기"), T("Half (less memory)", "절반 (메모리 절약)") };
        RowLabel(T("Size", "크기"));
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##quality", &g_pc_settings.hd_quality, names, 2)) {
            pc_settings_save();
            pc_texture_pack_reload();
        }
    }
    if (info.entries > 0) {
        size_text(info.gpu_bytes, a, sizeof(a));
        size_text(info.budget, b, sizeof(b));
        if (info.budget > 0) {
            snprintf(buf, sizeof(buf),
                     T("%d textures replaced so far, %d in memory (%s of %s)%s", "지금까지 %d개 교체, 메모리에 %d개 (%s / %s)%s"),
                     info.replaced, info.loaded, a, b,
                     info.loading > 0 ? T(", loading...", ", 불러오는 중...") : "");
        } else {
            snprintf(buf, sizeof(buf), T("%d textures replaced so far, %d in memory (%s)", "지금까지 %d개 교체, 메모리에 %d개 (%s)"),
                     info.replaced, info.loaded, a);
        }
        Help(buf);
        if (!info.gpu_bc7) {
            Help(T("This GPU cannot take BC7: textures are decoded on the CPU (slower, more memory).",
                   "이 GPU는 BC7을 직접 못 읽어서 CPU에서 풀어 씁니다 (느리고 메모리를 더 씀)."));
        }
    }
    Help(T("Loading mode and size take effect after a restart.", "불러오기 방식은 다시 시작하면 적용됩니다."));

    Heading(T("Korean patch", "한글패치와 함께"));
    if (g_pc_ko_mode) {
        snprintf(buf, sizeof(buf),
                 T("The game runs in Korean. Textures the patch changed (logo, name plates, text) never match the pack, "
                   "so they stay as the patch made them. %d textures of other languages (Localisation) are left out.",
                   "지금 한국어로 실행 중입니다. 패치가 바꾼 텍스처(로고, 이름표, 글자)는 팩과 해시가 달라 바뀌지 않고 패치 "
                   "그대로 나옵니다. 다른 언어용 텍스처(Localisation) %d개는 쓰지 않습니다."),
                 info.skipped_korean);
        Help(buf);
    } else {
        Help(T("With the Korean patch, textures the patch changed are never replaced: packs match textures by the hash "
               "of their exact data.",
               "한글패치를 쓸 때도 패치가 바꾼 텍스처는 바뀌지 않습니다: 텍스처팩은 원래 데이터의 해시로만 맞춰 바꿉니다."));
    }

    Heading(T("Where the pack comes from", "텍스처팩 출처"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(T("Animal Crossing HD Texture Pack (version 25, October 11th 2026)",
                             "Animal Crossing HD Texture Pack (버전 25, 2026년 10월 11일)"));
    ImGui::PopTextWrapPos();
    Help(T("Redrawn by hand by TechieAndroid and the contributors of the Dolphin forums project (since 2016). "
           "Made for Dolphin; it is not part of this port, which only reads it.",
           "TechieAndroid와 Dolphin 포럼 프로젝트 기여자들이 2016년부터 손으로 다시 그린 텍스처팩입니다. Dolphin용으로 "
           "만들어졌으며 이 포트에는 들어 있지 않고, 포트는 받은 파일을 읽기만 합니다."));
    RowLabel(T("Original post:", "원본 출처:"));
    Link("forums.dolphin-emu.org - Animal Crossing HD Texture Pack (Version 25)", PACK_THREAD);
    RowLabel(T("Download:", "다운로드:"));
    Link("drive.google.com/file/d/1Z8-pMx20xO8q3caiqy4NOrSNVT1veDSk", PACK_DOWNLOAD);
}

bool TexturePackBack() {
    if (s_remove_open) {
        s_remove_open = false;
        return true;
    }
    if (s_install_open) {
        if (pc_texture_pack_install_state() == PC_TEXPACK_INSTALL_RUNNING) {
            pc_texture_pack_install_cancel();
        } else {
            pc_texture_pack_install_reset();
            s_install_open = false;
        }
        return true;
    }
    return false;
}

static bool modal(const char* id, bool open) {
    if (!open) {
        return false;
    }
    if (!ImGui::IsPopupOpen(id)) {
        ImGui::OpenPopup(id);
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

void TexturePackPopups() {
    if (s_install_open) {
        pc_texture_pack_install_poll();
    }
    if (modal("##texinstall", s_install_open)) {
        PCTexPackInstallState st = pc_texture_pack_install_state();

        Heading(T("HD texture pack", "HD 텍스처팩"));
        if (st == PC_TEXPACK_INSTALL_RUNNING) {
            ImGui::TextWrapped("%s", pc_texture_pack_install_message());
            Progress(pc_texture_pack_install_progress());
            if (BigButton(T("Cancel", "취소"))) {
                pc_texture_pack_install_cancel();
            }
        } else {
            if (st == PC_TEXPACK_INSTALL_DONE) {
                ImGui::TextColored(Vec(ColGood()), "%s", T("Installed and turned on.", "설치하고 켰습니다."));
                ImGui::TextWrapped("%s", pc_texture_pack_install_message());
                Help(T("Textures change as the game draws them; no restart needed.",
                       "게임이 그리는 대로 바로 바뀝니다. 다시 시작할 필요 없습니다."));
            } else {
                ImGui::TextColored(Vec(ColWarn()), "%s", T("Could not install it", "설치하지 못했습니다"));
                ImGui::TextWrapped("%s", pc_texture_pack_install_message());
            }
            if (BigButton("OK", -1, true)) {
                pc_texture_pack_install_reset();
                s_install_open = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (modal("##texremove", s_remove_open)) {
        char buf[256];

        snprintf(buf, sizeof(buf), T("Delete %s from texture_pack?", "texture_pack에서 %s을(를) 지울까요?"), s_remove_name);
        ImGui::TextWrapped("%s", buf);
        float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (BigButton(T("Cancel", "취소"), w)) {
            s_remove_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (BigButton(T("Delete", "삭제"), w, true)) {
            pc_texture_pack_remove(s_remove_name);
            s_remove_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace gui
