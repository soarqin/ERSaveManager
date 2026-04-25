/**
 * @file toolbar.h
 * @brief Top toolbar widget: backup profile combobox + action buttons.
 * @details Provides a fixed-height (30px) child container hosting a backup
 *          profile selector combobox plus six action buttons:
 *          add backup, delete backup, backup full, backup slot, restore, undo.
 *          The toolbar is purely a UI container; WM_COMMAND routing for the
 *          action buttons is handled by the main window procedure.
 */

#pragma once

#include <stdbool.h>
#include <windows.h>

#include "profile_store.h"

/* Opaque toolbar handle. */
typedef struct toolbar_s toolbar_t;

/**
 * @brief Create the toolbar as a child of the parent window.
 * @param parent Parent window handle.
 * @param hinst  Application instance handle.
 * @return Heap-allocated toolbar_t on success, NULL on failure.
 */
toolbar_t *toolbar_create(HWND parent, HINSTANCE hinst);

/**
 * @brief Destroy the toolbar and free resources.
 * @param t Toolbar handle (may be NULL).
 */
void toolbar_destroy(toolbar_t *t);

/**
 * @brief Get the toolbar's window handle (the container child window).
 * @param t Toolbar handle.
 * @return Container window handle, or NULL if t is NULL.
 */
HWND toolbar_get_hwnd(const toolbar_t *t);

/**
 * @brief Get the fixed height of the toolbar in pixels.
 * @param t Toolbar handle.
 * @return Toolbar height in pixels (always 30), or 0 if t is NULL.
 */
int toolbar_get_height(const toolbar_t *t);

/**
 * @brief Reflow the toolbar layout for a new parent width.
 * @details Combobox stretches to fill available space; buttons stay
 *          right-aligned. Minimum combobox width is clamped to 120 px.
 * @param t Toolbar handle.
 * @param parent_width Width of parent client area in pixels.
 */
void toolbar_layout(toolbar_t *t, int parent_width);

/**
 * @brief Repopulate the backup profile combobox from the profile store.
 * @details Clears existing items then for each backup profile in the store
 *          appends a label of the form "<game_name> / <backup_name>" with
 *          the backup_id stored as item data via CB_SETITEMDATA.
 * @param t     Toolbar handle.
 * @param store Profile store to read from (may be NULL — clears combobox).
 */
void toolbar_populate_profiles(toolbar_t *t, const profile_store_t *store);

/**
 * @brief Get the currently selected backup profile ID from the combobox.
 * @param t Toolbar handle.
 * @return Backup profile ID, or 0 if no selection or t is NULL.
 */
int toolbar_get_selected_backup_id(const toolbar_t *t);

/**
 * @brief Select a backup profile in the combobox by its ID.
 * @details If no item with the given ID exists, the selection is cleared.
 * @param t         Toolbar handle.
 * @param backup_id Backup profile ID to select.
 */
void toolbar_set_selected_backup_id(toolbar_t *t, int backup_id);

/**
 * @brief Enable or disable all action buttons.
 * @details When disabled, only the combobox and the "+" (add backup) button
 *          remain enabled so users can still create their first profile.
 *          The "-", Backup Full, Backup Slot, Restore, and Undo buttons are
 *          gated by this flag.
 * @param t       Toolbar handle.
 * @param enabled true to enable action buttons, false to disable.
 */
void toolbar_set_actions_enabled(toolbar_t *t, bool enabled);
