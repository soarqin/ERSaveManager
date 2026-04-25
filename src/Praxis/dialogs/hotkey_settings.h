/**
 * @file hotkey_settings.h
 * @brief Modal dialog for editing the four global hotkey bindings.
 */

#pragma once

#include <stdbool.h>
#include <windows.h>

/**
 * @brief Show the Hotkey Settings modal dialog.
 * @details Displays a HOTKEY common control for each of the four global
 *          actions (Backup Full, Backup Slot, Restore, Undo Restore).
 *          On OK the bindings are validated, written to praxis_config,
 *          persisted via save_profile_store(), and re-registered with the
 *          system. On Cancel no changes are made.
 * @param parent Parent window handle (may be NULL).
 * @return true if the user accepted and bindings were applied successfully,
 *         false on Cancel or registration failure.
 */
bool dialog_hotkey_settings_show(HWND parent);
