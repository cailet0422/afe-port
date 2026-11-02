/* pc_gui_mp.cpp - the multiplayer page (filled in with pc_mp.c) */
#include "pc_gui_internal.h"

namespace gui {

void MultiplayerPage(bool* back) {
    *back = true;
}
void MultiplayerPopups() {}
bool MultiplayerBack() { return false; }
const char* MultiplayerStatus() { return nullptr; }
void MultiplayerOverlay() {}
bool MultiplayerOverlayWanted() { return false; }

} // namespace gui
