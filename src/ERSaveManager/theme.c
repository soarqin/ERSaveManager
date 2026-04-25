/**
 * @file theme.c
 * @brief ERSaveManager theme glue implementation.
 */

#include "theme.h"

#include "config.h"
#include "locale.h"
#include "resource.h"
#include "theme_core.h"

#include <stdbool.h>
#include <windows.h>

/* Globals defined in main.c */
extern HWND main_window;
extern HMENU menu_bar;

/* The Theme submenu handle, retained for runtime updates. */
static HMENU s_theme_submenu = NULL;

void theme_init_from_config(void) {
    theme_core_init();
    theme_core_set_mode((theme_mode_t)config.theme);
}

void theme_apply_to_window(HWND hwnd) {
    if (!hwnd) {
        return;
    }
    theme_core_apply_to_window_and_children(hwnd);
}

bool theme_is_menu_command(int id) {
    return id >= IDM_THEME_SYSTEM && id <= IDM_THEME_DARK;
}

void theme_switch_mode(int mode) {
    if (mode < THEME_MODE_SYSTEM || mode > THEME_MODE_DARK) {
        return;
    }
    /* Always re-apply on click; persist only when the value actually changed.
     * Clicking the already-active mode still re-resolves system preference. */
    bool changed = (mode != config.theme);
    config.theme = mode;
    theme_core_set_mode((theme_mode_t)mode);
    if (changed) {
        save_config();
    }
    theme_refresh_menu_check();
    if (main_window) {
        theme_core_apply_to_window_and_children(main_window);
    }
}

void theme_refresh_menu_check(void) {
    if (!s_theme_submenu) {
        return;
    }
    static const UINT k_cmds[] = { IDM_THEME_SYSTEM, IDM_THEME_LIGHT, IDM_THEME_DARK };
    static const int  k_modes[] = { THEME_MODE_SYSTEM, THEME_MODE_LIGHT, THEME_MODE_DARK };
    for (int i = 0; i < 3; i++) {
        UINT state = (config.theme == k_modes[i]) ? MF_CHECKED : MF_UNCHECKED;
        CheckMenuItem(s_theme_submenu, k_cmds[i], MF_BYCOMMAND | state);
    }
}

void theme_rebuild_submenu_strings(void) {
    if (!s_theme_submenu) {
        return;
    }
    /* Wipe all items and re-add to refresh strings for new locale. */
    while (GetMenuItemCount(s_theme_submenu) > 0) {
        RemoveMenu(s_theme_submenu, 0, MF_BYPOSITION);
    }
    AppendMenuW(s_theme_submenu, MF_STRING, IDM_THEME_SYSTEM, locale_str(STR_THEME_SYSTEM));
    AppendMenuW(s_theme_submenu, MF_STRING, IDM_THEME_LIGHT,  locale_str(STR_THEME_LIGHT));
    AppendMenuW(s_theme_submenu, MF_STRING, IDM_THEME_DARK,   locale_str(STR_THEME_DARK));
    theme_refresh_menu_check();
}

HMENU theme_build_submenu(HMENU parent_menu) {
    if (!parent_menu) {
        return NULL;
    }
    s_theme_submenu = CreatePopupMenu();
    if (!s_theme_submenu) {
        return NULL;
    }
    AppendMenuW(s_theme_submenu, MF_STRING, IDM_THEME_SYSTEM, locale_str(STR_THEME_SYSTEM));
    AppendMenuW(s_theme_submenu, MF_STRING, IDM_THEME_LIGHT,  locale_str(STR_THEME_LIGHT));
    AppendMenuW(s_theme_submenu, MF_STRING, IDM_THEME_DARK,   locale_str(STR_THEME_DARK));
    AppendMenuW(parent_menu, MF_POPUP, (UINT_PTR)s_theme_submenu, locale_str(STR_THEME));
    theme_refresh_menu_check();
    return s_theme_submenu;
}

void theme_cleanup(void) {
    /* Submenu is owned by the parent menu; do not destroy here. */
    s_theme_submenu = NULL;
    theme_core_cleanup();
}

void theme_handle_menu_command(int id) {
    int mode;
    switch (id) {
    case IDM_THEME_SYSTEM: mode = THEME_MODE_SYSTEM; break;
    case IDM_THEME_LIGHT:  mode = THEME_MODE_LIGHT;  break;
    case IDM_THEME_DARK:   mode = THEME_MODE_DARK;   break;
    default: return;
    }
    theme_switch_mode(mode);
}
