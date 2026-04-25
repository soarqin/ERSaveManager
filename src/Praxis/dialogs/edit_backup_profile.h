/**
 * @file edit_backup_profile.h
 * @brief Modal dialog for adding or editing a single backup profile.
 */

#pragma once

#include <stdbool.h>
#include <windows.h>

#include "../profile_store.h"

/**
 * @brief Show the Edit Backup Profile modal dialog.
 * @param parent Parent window handle (may be NULL).
 * @param bp_inout Backup profile to populate (input on edit; output on OK).
 * @param parent_game_tree_root Tree root of the parent game profile, used to auto-suggest
 *                              child tree_root paths as the user types the name.
 * @param is_new true to create a new profile (defaults to Low compression and auto-suggests
 *               tree_root); false to edit existing.
 * @return IDOK if the user accepted, IDCANCEL otherwise.
 */
INT_PTR edit_backup_profile(HWND parent, backup_profile_t *bp_inout,
    const wchar_t *parent_game_tree_root, bool is_new);
