/**
 * @file migration_wizard.h
 * @brief First-launch migration wizard for converting legacy single-profile INI to multi-profile schema.
 */

#pragma once

#include <stdbool.h>
#include <windows.h>

#include "../profile_store.h"

/**
 * @brief Run the migration wizard.
 * @param parent Parent window handle (may be NULL).
 * @param store Profile store to populate on success.
 * @param ini_path Path to Praxis.ini (used for both reading legacy values and writing migrated output).
 * @param legacy_tree_root Pre-detected legacy tree root (used as default for the Backup Root field).
 * @param legacy_compression_level Pre-detected legacy compression level (1, 5, or 9; mapped to none/low/high).
 * @return true if the user clicked Finish (migration applied + saved); false if cancelled.
 */
bool run_migration_wizard(HWND parent, profile_store_t *store, const wchar_t *ini_path,
    const wchar_t *legacy_tree_root, int legacy_compression_level);
