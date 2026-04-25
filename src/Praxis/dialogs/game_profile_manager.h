/**
 * @file game_profile_manager.h
 * @brief Game Profile Manager modal dialog.
 * @details Lists all configured game profiles in a ListView and lets the user add,
 *          edit, or delete game profiles. Deleting a game profile cascades to all of
 *          its child backup profiles.
 */

#pragma once

#include <windows.h>

#include "../profile_store.h"

/**
 * @brief Show the Game Profile Manager modal dialog.
 * @param parent Parent window handle (may be NULL).
 * @param store Profile store to inspect/modify; persisted via profile_store_save on changes.
 * @param ini_path Path to the Praxis.ini used for atomic save after each CRUD operation.
 * @return IDOK after the user closes the dialog.
 */
INT_PTR dialog_game_profile_manager_show(HWND parent, profile_store_t *store, const wchar_t *ini_path);
