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
 * @param gp_inout Game profile to populate (input on edit; output on OK).
 * @param is_new true to create a new profile (sets defaults), false to edit existing.
 * @return IDOK if the user accepted, IDCANCEL otherwise.
 */
INT_PTR edit_game_profile(HWND parent, game_profile_t *gp_inout, bool is_new);
