/**
 * @file edit_game_profile.h
 * @brief Modal dialog for adding or editing a single game profile.
 */

#pragma once

#include <stdbool.h>
#include <windows.h>

#include "../profile_store.h"

/**
 * @brief Show the Edit Game Profile modal dialog.
 * @param parent Parent window handle (may be NULL).
 * @param store Profile store used to find a unique default name and (in the future)
 *              other context-sensitive defaults. May be NULL when no store is
 *              available (e.g. early migration); auto-fill then assumes empty store.
 * @param gp_inout Game profile to populate (input on edit; output on OK).
 *                 For is_new, any non-empty fields are honored as caller pre-fills.
 * @param is_new true to create a new profile (auto-fills empty fields with sensible
 *               defaults), false to edit existing.
 * @return IDOK if the user accepted, IDCANCEL otherwise.
 */
INT_PTR dialog_edit_game_profile_show(HWND parent,
                                      const profile_store_t *store,
                                      game_profile_t *gp_inout,
                                      bool is_new);
