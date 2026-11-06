/* pc_gui_render.cpp - Dear ImGui's draw lists through pc_gfx (any backend).
 *
 * Does what imgui_impl_opengl3 did: the font atlas and the other textures
 * ImGui asks for (ImGuiBackendFlags_RendererHasTextures), then each draw
 * command as indexed triangles with its clip rectangle as the scissor box,
 * alpha blended (the alpha as ImGui's backends blend it). */
#include "imgui.h"
#include "pc_gfx.h"

#include <stdint.h>

namespace gui {

static ImVector<PCGfxUIVertex> s_vertices;

void pc_gui_render_init(void) {
    ImGuiIO& io = ImGui::GetIO();

    io.BackendRendererName = "pc_gfx";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
}

static void update_texture(ImTextureData* tex) {
    if (tex->Status == ImTextureStatus_WantCreate) {
        PCTex t = pc_gfx_tex_new();

        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);
        pc_gfx_tex_upload(t, tex->Width, tex->Height, PC_TEXF_RGBA8, tex->GetPixels(),
                          (size_t)tex->Width * tex->Height * 4, PC_FILTER_LINEAR);
        tex->SetTexID((ImTextureID)(intptr_t)t);
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
        PCTex t = (PCTex)(intptr_t)tex->TexID;

        for (ImTextureRect& r : tex->Updates) {
            pc_gfx_tex_update(t, r.x, r.y, r.w, r.h, tex->Width, tex->GetPixelsAt(r.x, r.y));
        }
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        pc_gfx_tex_delete((PCTex)(intptr_t)tex->TexID);
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}

/* draw 0: only the textures ImGui asked for (a frame that is not shown) */
void pc_gui_render_draw(ImDrawData* dd, int fb_w, int fb_h, int draw) {
    ImVec2 off = dd->DisplayPos, scale = dd->FramebufferScale;

    if (dd->Textures != NULL) {
        for (ImTextureData* tex : *dd->Textures) {
            if (tex->Status != ImTextureStatus_OK) update_texture(tex);
        }
    }
    if (!draw || fb_w <= 0 || fb_h <= 0) return;

    pc_gfx_viewport(0, 0, fb_w, fb_h);
    pc_gfx_blend(1);
    pc_gfx_blend_subtract(0);
    pc_gfx_blend_func_separate(PC_BF_SRC_ALPHA, PC_BF_ONE_MINUS_SRC_ALPHA, PC_BF_ONE, PC_BF_ONE_MINUS_SRC_ALPHA);
    pc_gfx_cull(PC_CULL_NONE);
    pc_gfx_depth_test(0);
    pc_gfx_color_write(1, 1);
    pc_gfx_scissor_test(1);

    for (const ImDrawList* list : dd->CmdLists) {
        const ImDrawVert* src = list->VtxBuffer.Data;

        s_vertices.resize(list->VtxBuffer.Size);
        for (int i = 0; i < list->VtxBuffer.Size; i++) {
            PCGfxUIVertex* v = &s_vertices[i];
            ImU32 c = src[i].col;

            v->x = src[i].pos.x - off.x;
            v->y = src[i].pos.y - off.y;
            v->u = src[i].uv.x;
            v->v = src[i].uv.y;
            v->lx = v->ly = v->kind = v->inner = 0.0f;
            v->color[0] = (unsigned char)(c >> IM_COL32_R_SHIFT);
            v->color[1] = (unsigned char)(c >> IM_COL32_G_SHIFT);
            v->color[2] = (unsigned char)(c >> IM_COL32_B_SHIFT);
            v->color[3] = (unsigned char)(c >> IM_COL32_A_SHIFT);
        }
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback != NULL) {
                if (cmd.UserCallback != ImDrawCallback_ResetRenderState) cmd.UserCallback(list, &cmd);
                continue;
            }
            float x0 = (cmd.ClipRect.x - off.x) * scale.x, y0 = (cmd.ClipRect.y - off.y) * scale.y;
            float x1 = (cmd.ClipRect.z - off.x) * scale.x, y1 = (cmd.ClipRect.w - off.y) * scale.y;

            if (x0 < 0.0f) x0 = 0.0f;
            if (y0 < 0.0f) y0 = 0.0f;
            if (x1 > (float)fb_w) x1 = (float)fb_w;
            if (y1 > (float)fb_h) y1 = (float)fb_h;
            if (x1 <= x0 || y1 <= y0 || cmd.ElemCount == 0) continue;
            /* (the scissor box counts from the bottom) */
            pc_gfx_scissor((int)x0, (int)((float)fb_h - y1), (int)(x1 - x0), (int)(y1 - y0));

            const ImDrawIdx* idx = list->IdxBuffer.Data + cmd.IdxOffset;
            unsigned top = 0;

            for (unsigned i = 0; i < cmd.ElemCount; i++) {
                if (idx[i] > top) top = idx[i];
            }
            pc_gfx_draw_ui((PCTex)(intptr_t)cmd.GetTexID(), &s_vertices[(int)cmd.VtxOffset], (int)top + 1, idx,
                           (int)cmd.ElemCount, dd->DisplaySize.x, dd->DisplaySize.y);
        }
    }
    pc_gfx_scissor_test(0);
}

void pc_gui_render_shutdown(void) {
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
        if (tex->RefCount == 1 && tex->TexID != ImTextureID_Invalid) {
            pc_gfx_tex_delete((PCTex)(intptr_t)tex->TexID);
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

} // namespace gui
