/* pc_gui_internal.h - shared between the GUI's files (C++, Dear ImGui) */
#ifndef PC_GUI_INTERNAL_H
#define PC_GUI_INTERNAL_H

#include "imgui.h"
#include "imgui_internal.h"

#include "pc_gui.h"
#include "pc_settings.h"

namespace gui {

/* The UI's size factor (1 = 1280x720 on a desktop) */
extern float g_scale;
/* The last pointer input was a finger: bigger gaps, drag to scroll */
extern bool g_touch;

/* English or Korean, as the UI language is */
const char* T(const char* en, const char* ko);
bool Korean();

/* Colours of the theme */
ImU32 ColAccent();
ImU32 ColText();
ImU32 ColMuted();
ImU32 ColWarn();
ImU32 ColGood();
ImVec4 Vec(ImU32 c);

/* Widgets */
bool BigButton(const char* label, float width = -1.0f, bool primary = false);
bool SmallButton(const char* label);
/* An on/off switch with its label on the left of the row; 1 if it changed */
bool Switch(const char* label, int* value, const char* help = nullptr);
/* A row label in the left column of a settings row */
void RowLabel(const char* label, const char* help = nullptr);
/* Grey explanation text, wrapped */
void Help(const char* text);
void Heading(const char* text);
/* Lets a finger drag the current window's contents (call inside it) */
void TouchScroll();
/* A progress bar of permille (0-1000) with the percentage on it */
void Progress(int permille);
/* Text with a link that opens the browser */
void Link(const char* text, const char* url);
/* Centres the next item of the given width */
void CenterNext(float width);
float ButtonHeight();

/* Pause menu (pc_gui_menu.cpp) */
void MenuOpen();
void MenuClose();
void MenuOpenUpdate();
void MenuDraw();
/* Esc / B: closes what is on top; false if there is nothing left (close the menu) */
bool MenuBack();
bool MenuCapturing();
void MenuCaptureEvent(const union SDL_Event* e);
bool MenuWantsKeyboard();

/* Settings tab for the HD texture pack (pc_gui_textures.cpp) */
void TexturePackTab();
void TexturePackPopups();
bool TexturePackBack();

/* Multiplayer (pc_gui_mp.cpp) */
void MultiplayerPage(bool* back);
void MultiplayerPopups();
bool MultiplayerBack();
const char* MultiplayerStatus(); /* one line for the main page, or NULL */
void MultiplayerOverlay();      /* name tags and the room bar while playing */
bool MultiplayerOverlayWanted();

/* Messages at the top while playing */
void DrawNotifications();
bool NotificationsWanted();

} // namespace gui

#endif
