/**
 * @file theme.h
 * @brief ERSaveManager theme glue (light/dark mode integration).
 * @details Bridges theme_core to ERSaveManager's specific UI surfaces. Handles
 *          the Theme submenu, applying the theme to the main window and dialogs,
 *          and reacting to runtime theme changes.
 */

#pragma once

#include <stdbool.h>
#include <windows.h>

/**
 * @brief Initialise the theme system from current config.
 * @details Calls theme_core_init() and theme_core_set_mode(config.theme).
 *          Must be called once at application startup, BEFORE creating any windows.
 */
void theme_init_from_config(void);

/**
 * @brief Apply the current theme to a window and all its descendants.
 * @details For top-level windows this also updates the DWM titlebar. For dialogs
 *          this applies the dark border treatment. Forces a full redraw.
 */
void theme_apply_to_window(HWND hwnd);

/**
 * @brief Switch to a new theme mode at runtime.
 * @details Updates the global mode, persists to config, refreshes the menu
 *          checkmark, and re-applies theming to all known top-level windows.
 * @param mode One of THEME_MODE_SYSTEM/LIGHT/DARK (matches theme_core_t values).
 */
void theme_switch_mode(int mode);

/**
 * @brief Refresh the Theme submenu checkmark to match the current mode.
 */
void theme_refresh_menu_check(void);

/**
 * @brief Rebuild the Theme submenu's localized strings (called on language switch).
 */
void theme_rebuild_submenu_strings(void);

/**
 * @brief Build the Theme submenu and append it to the given parent menu.
 * @param parent_menu  Parent (typically the Options menu).
 * @return The created submenu handle (caller does NOT need to free; the parent owns it).
 */
HMENU theme_build_submenu(HMENU parent_menu);

/**
 * @brief Cleanup theme resources before app exit.
 */
void theme_cleanup(void);

/**
 * @brief Test whether @p id is in the Theme menu command range.
 */
bool theme_is_menu_command(int id);

/**
 * @brief Handle a Theme menu command. Caller has already verified the ID
 *        with theme_is_menu_command().
 */
void theme_handle_menu_command(int id);
