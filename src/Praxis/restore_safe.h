/**
 * @file restore_safe.h
 * @brief Safe restore wrapper: ring snapshot before restore, abort on failure.
 */

#pragma once

#include "game_backend.h"
#include <stdbool.h>
#include <wchar.h>

bool restore_with_safety(const game_backend_t *backend,
                         const wchar_t *backup_src,
                         const wchar_t *save_dst,
                         const wchar_t *tree_root,
                         int compression_level,
                         bool slot_mode,
                         int slot_index);

/**
 * @brief Restore from backup file, auto-detecting full vs slot save.
 * @details Reads up to 21 bytes to classify the backup via
 *          save_compress_classify_backup. For SAVE_KIND_FULL calls
 *          restore_with_safety with slot_mode=false. For SAVE_KIND_SLOT
 *          queries backend->get_active_slot then calls restore_with_safety
 *          with slot_mode=true. For SAVE_KIND_UNKNOWN returns false.
 *          Always takes a pre-restore ring snapshot (via restore_with_safety).
 * @param backend Game backend vtable
 * @param backup_src Path to backup file (.ersm or .sl2)
 * @param save_dst Path to active save file
 * @param tree_root Ring backup directory root
 * @param compression_level LZMA level for ring snapshot (1-9)
 * @return true on success, false on classification failure or restore failure
 */
bool restore_with_safety_auto(const game_backend_t *backend,
                              const wchar_t *backup_src,
                              const wchar_t *save_dst,
                              const wchar_t *tree_root,
                              int compression_level);

bool undo_last_restore(const game_backend_t *backend, const wchar_t *tree_root, int compression_level);
