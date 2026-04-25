/**
 * @file praxis_hotkey_actions.h
 * @brief Hotkey-triggered backup/restore action handlers.
 * @details Implements the four core backup/restore operations invoked from the
 *          WM_HOTKEY message and the toolbar action buttons. Each function
 *          operates on the active game/backup profile from the provided store
 *          and refreshes the save tree widget on success.
 */

#pragma once

#include <stdbool.h>
#include <windows.h>
#include "profile_store.h"
#include "save_tree.h"

/**
 * @brief Perform a full-save backup using the active backup profile.
 * @details Creates a timestamped backup of the active save file using the
 *          backend-defined extension (e.g. `.ersm`). When compression_level
 *          is COMP_LEVEL_NONE the file is a byte-identical raw BND4 copy of
 *          the source save; otherwise it is an LZMA-compressed ERSM
 *          container. The format is identified by magic bytes at restore
 *          time, not by the extension.
 *          Refreshes save_tree and selects the new file on success.
 * @param hwnd Main window handle (reserved for future MessageBox feedback).
 * @param store Profile store containing the active game/backup profile.
 * @param save_tree Save tree widget to refresh and select after backup.
 * @param compression_level compression_level_t value cast to int.
 * @return true on success, false on any error.
 */
bool praxis_hotkey_action_backup_full(HWND hwnd, profile_store_t *store,
                                      save_tree_t *save_tree, int compression_level);

/**
 * @brief Perform an active-slot backup using the active backup profile.
 * @details Queries the backend for the currently active character slot, then
 *          creates a timestamped backup of that slot using the
 *          backend-defined extension (e.g. `.ersm`).
 *          Refreshes save_tree and selects the new file on success.
 * @param hwnd Main window handle (reserved for future MessageBox feedback).
 * @param store Profile store containing the active game/backup profile.
 * @param save_tree Save tree widget to refresh and select after backup.
 * @param compression_level compression_level_t value cast to int.
 * @return true on success, false on any error.
 */
bool praxis_hotkey_action_backup_slot(HWND hwnd, profile_store_t *store,
                                      save_tree_t *save_tree, int compression_level);

/**
 * @brief Restore the currently selected backup to the active save.
 * @details Takes a pre-restore ring snapshot, then auto-detects whether the
 *          selected backup is a full save or a slot save and restores
 *          accordingly. Refreshes save_tree on success.
 * @param hwnd Main window handle (reserved for future MessageBox feedback).
 * @param store Profile store containing the active game/backup profile.
 * @param save_tree Save tree widget providing the selected path; refreshed on success.
 * @return true on success, false on any error.
 */
bool praxis_hotkey_action_restore(HWND hwnd, profile_store_t *store, save_tree_t *save_tree);

/**
 * @brief Undo the last restore by re-applying the pre-restore ring snapshot.
 * @details Reads last-restore metadata from the ring directory and calls
 *          restore_safe_undo. The caller is responsible for refreshing any
 *          UI tree widgets after a successful undo.
 * @param hwnd Main window handle (reserved for future MessageBox feedback).
 * @param store Profile store containing the active game/backup profile.
 * @return true on success, false on any error.
 */
bool praxis_hotkey_action_undo(HWND hwnd, profile_store_t *store);
