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
 * @param is_new true to create a new profile (defaults to Low compression);
 *               false to edit existing.
 * @return IDOK if the user accepted, IDCANCEL otherwise.
 */
INT_PTR dialog_edit_backup_profile_show(HWND parent, backup_profile_t *bp_inout, bool is_new);
