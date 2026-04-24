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

bool undo_last_restore(const game_backend_t *backend, const wchar_t *tree_root, int compression_level);
