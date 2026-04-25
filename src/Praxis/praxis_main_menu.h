/**
 * @file praxis_main_menu.h
 * @brief Dynamic main menu construction and command dispatch.
 */

#pragma once

#include <stdbool.h>

#include <windows.h>

#include "profile_store.h"

/**
 * @brief Apply localized strings to the main menu bar.
 * @param hwnd Main window handle.
 */
void praxis_main_menu_apply_locale_strings(HWND hwnd);

/**
 * @brief Rebuild the dynamic submenu items (game profiles, languages).
 * @param hwnd  Main window handle (reserved, not currently used).
 * @param hmenu Popup menu handle received via WM_INITMENUPOPUP wParam.
 * @param store Active profile store (read-only).
 */
void praxis_main_menu_init_popup(HWND hwnd, HMENU hmenu, const profile_store_t *store);

/**
 * @brief Handle a WM_COMMAND notification for menu items in our ID range.
 * @param hwnd   Main window handle.
 * @param wparam WM_COMMAND wParam.
 * @param store  Active profile store (mutable).
 * @return true if handled; false if caller should fall through.
 */
bool praxis_main_menu_handle_command(HWND hwnd, WPARAM wparam, profile_store_t *store);
