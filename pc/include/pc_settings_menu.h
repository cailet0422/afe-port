/* pc_settings_menu.h - the settings are pages of the pause menu (pc_gui) */
#ifndef PC_SETTINGS_MENU_H
#define PC_SETTINGS_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

/* 1 while the menus own the screen (the title demo holds its clock meanwhile) */
int pc_settings_menu_active(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_SETTINGS_MENU_H */
