/**
 * @file restore_safe.h
 * @brief Safe restore wrapper: ring snapshot before restore, abort on failure.
 */

#pragma once

#include "game_backend.h"
#include <stdbool.h>
#include <wchar.h>

/**
 * @brief Parameters for a safe restore operation.
 */
typedef struct restore_safe_request_s {
    const game_backend_t *backend;  /**< Game backend vtable */
    const wchar_t *backup_src;      /**< Path to backup file; format detected by magic bytes (legacy `.sl2` and current `.ersm` both supported) */
    const wchar_t *save_dst;        /**< Path to active save file */
    const wchar_t *tree_root;       /**< Ring backup directory root */
    int compression_level;          /**< LZMA level for ring snapshot (1-9) */
    bool slot_mode;                 /**< false=full restore, true=single slot restore */
    int slot_index;                 /**< Slot index (0-based); ignored when slot_mode is false */
} restore_safe_request_t;

/**
 * @brief Restore a backup file to the active save, taking a ring snapshot first.
 * @details Calls ring_backup_snapshot before restoring so the restore can be
 *          undone via restore_safe_undo. For slot_mode=true uses
 *          backend->restore_slot; otherwise uses backend->restore_full.
 *          Writes last_restore metadata after a successful restore.
 * @param req Pointer to a populated restore_safe_request_t struct
 * @return true on success, false if ring snapshot or restore fails
 */
bool restore_safe_full(const restore_safe_request_t *req);

/**
 * @brief Restore from backup file, auto-detecting full vs slot save.
 * @details Reads up to 21 bytes to classify the backup via
 *          save_compress_classify_backup. For SAVE_KIND_FULL calls
 *          restore_safe_full with slot_mode=false. For SAVE_KIND_SLOT
 *          queries backend->get_active_slot then calls restore_safe_full
 *          with slot_mode=true. For SAVE_KIND_UNKNOWN returns false.
 *          Always takes a pre-restore ring snapshot (via restore_safe_full).
 * @param backend Game backend vtable
 * @param backup_src Path to backup file; format detected by magic bytes (legacy `.sl2` and current `.ersm` both supported)
 * @param save_dst Path to active save file
 * @param tree_root Ring backup directory root
 * @param compression_level LZMA level for ring snapshot (1-9)
 * @return true on success, false on classification failure or restore failure
 */
bool restore_safe_auto(const game_backend_t *backend,
                              const wchar_t *backup_src,
                              const wchar_t *save_dst,
                              const wchar_t *tree_root,
                              int compression_level);

/**
 * @brief Undo the last restore by re-applying the pre-restore ring snapshot.
 * @details Reads last_restore metadata written by restore_safe_full to
 *          locate the ring backup and target save path, then calls
 *          restore_safe_full on the ring file (enabling redo via another undo).
 * @param backend Game backend vtable
 * @param tree_root Ring backup directory root
 * @param compression_level LZMA level for the new ring snapshot taken by this undo
 * @return true on success, false if metadata is missing or restore fails
 */
bool restore_safe_undo(const game_backend_t *backend, const wchar_t *tree_root, int compression_level);
