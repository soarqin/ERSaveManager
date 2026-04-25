/**
 * @file theme.c
 * @brief Praxis theme glue implementation.
 */

#include "theme.h"

#include "config.h"
#include "locale.h"
#include "resource.h"
#include "theme_core.h"
#include "praxis_window_common.h"

#include <stdbool.h>
#include <windows.h>

/* External hooks. */
extern bool save_profile_store(void);

void praxis_theme_init_from_config(void) {
    theme_core_init();
    theme_core_set_mode((theme_mode_t)praxis_config.theme);
}

void praxis_theme_apply_to_window(HWND hwnd) {
    if (!hwnd) {
        return;
    }
    theme_core_apply_to_window_and_children(hwnd);
}

bool praxis_theme_is_menu_command(int id) {
    return id >= IDM_THEME_SYSTEM && id <= IDM_THEME_DARK;
}

void praxis_theme_switch_mode(int mode) {
    if (mode < THEME_MODE_SYSTEM || mode > THEME_MODE_DARK) {
        return;
    }
    /* Always re-apply on click. When the user clicks "System" while the
     * mode is unchanged, this lets us pick up any out-of-band OS preference
     * change. Persist to disk only when the value actually changed. */
    bool changed = (mode != praxis_config.theme);
    praxis_config.theme = mode;
    theme_core_set_mode((theme_mode_t)mode);
    if (changed) {
        save_profile_store();
    }
    if (g_app.main_window) {
        theme_core_apply_to_window_and_children(g_app.main_window);
    }
}

void praxis_theme_apply_locale_strings(HMENU options_menu) {
    /* Index 2 in Options menu = Theme (after Hotkey Settings at 0, Language at 1). */
    HMENU theme_submenu = GetSubMenu(options_menu, 2);
    if (!theme_submenu) {
        return;
    }
    ModifyMenuW(options_menu, 2, MF_BYPOSITION | MF_POPUP, (UINT_PTR)theme_submenu,
                praxis_locale_str(STR_PRAXIS_THEME));
}

void praxis_theme_init_popup(HMENU hmenu) {
    if (!hmenu) {
        return;
    }
    /* Wipe existing items (the .rc placeholder or prior dynamic entries). */
    while (GetMenuItemCount(hmenu) > 0) {
        DeleteMenu(hmenu, 0, MF_BYPOSITION);
    }
    /* Add System / Light / Dark entries with current locale strings. */
    static const struct {
        UINT id;
        praxis_string_index_t str;
        int mode;
    } k_items[] = {
        { IDM_THEME_SYSTEM, STR_PRAXIS_THEME_SYSTEM, THEME_MODE_SYSTEM },
        { IDM_THEME_LIGHT,  STR_PRAXIS_THEME_LIGHT,  THEME_MODE_LIGHT  },
        { IDM_THEME_DARK,   STR_PRAXIS_THEME_DARK,   THEME_MODE_DARK   },
    };
    for (int i = 0; i < 3; i++) {
        UINT flags = MF_BYPOSITION | MF_STRING;
        if (praxis_config.theme == k_items[i].mode) {
            flags |= MF_CHECKED;
        }
        InsertMenuW(hmenu, i, flags, k_items[i].id, praxis_locale_str(k_items[i].str));
    }
}

bool praxis_theme_is_theme_menu(HMENU hmenu) {
    if (!hmenu) {
        return false;
    }
    int count = GetMenuItemCount(hmenu);
    /* The .rc placeholder uses IDM_OPTIONS_THEME. After our first rebuild the
     * popup contains IDs in [IDM_THEME_SYSTEM, IDM_THEME_DARK]. Either is a
     * positive identification. */
    for (int i = 0; i < count; i++) {
        UINT id = GetMenuItemID(hmenu, i);
        if (id == IDM_OPTIONS_THEME ||
            (id >= IDM_THEME_SYSTEM && id <= IDM_THEME_DARK)) {
            return true;
        }
    }
    return false;
}

void praxis_theme_handle_menu_command(int id) {
    int mode;
    switch (id) {
    case IDM_THEME_SYSTEM: mode = THEME_MODE_SYSTEM; break;
    case IDM_THEME_LIGHT:  mode = THEME_MODE_LIGHT;  break;
    case IDM_THEME_DARK:   mode = THEME_MODE_DARK;   break;
    default: return;
    }
    praxis_theme_switch_mode(mode);
}

void praxis_theme_cleanup(void) {
    theme_core_cleanup();
}
