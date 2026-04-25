/**
 * @file theme.h
 * @brief Praxis theme glue (light/dark mode integration).
 * @details Bridges theme_core to Praxis's specific UI surfaces (main window,
 *          tree view, toolbar, status bar, modal dialogs). Handles the Theme
 *          submenu under Options and propagates theme changes at runtime.
 */

#pragma once

#include <stdbool.h>
#include <windows.h>

/**
 * @brief Initialise the theme system from praxis_config.theme.
 * @details Calls theme_core_init() and theme_core_set_mode(praxis_config.theme).
 *          Must be called once at startup before any window creation.
 */
void praxis_theme_init_from_config(void);

/**
 * @brief Apply the current theme to a window and all its descendants.
 */
void praxis_theme_apply_to_window(HWND hwnd);

/**
 * @brief Switch to a new theme mode at runtime.
 * @details Updates praxis_config.theme, persists profile store, refreshes
 *          menu checkmarks, and re-applies theme to the main window.
 */
void praxis_theme_switch_mode(int mode);

/**
 * @brief Apply localized strings to the Theme submenu (called from
 *        praxis_main_menu_apply_locale_strings).
 * @param options_menu Handle to the Options popup menu.
 */
void praxis_theme_apply_locale_strings(HMENU options_menu);

/**
 * @brief Rebuild the Theme submenu items (called from WM_INITMENUPOPUP when
 *        the Theme menu is opened).
 * @param hmenu Handle to the Theme submenu popup.
 */
void praxis_theme_init_popup(HMENU hmenu);

/**
 * @brief Test whether @p id is a Theme menu command.
 */
bool praxis_theme_is_menu_command(int id);

/**
 * @brief Handle a Theme menu command. Caller has verified the ID.
 */
void praxis_theme_handle_menu_command(int id);

/**
 * @brief Test whether the popup at @p hmenu is the Theme submenu.
 *        Used by WM_INITMENUPOPUP to identify which submenu was opened.
 */
bool praxis_theme_is_theme_menu(HMENU hmenu);

/**
 * @brief Cleanup theme resources before app exit.
 */
void praxis_theme_cleanup(void);
