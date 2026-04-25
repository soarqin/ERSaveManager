/**
 * @file ring_backup.h
 * @brief Pre-restore ring backup (FIFO rotation in .praxis_ring/).
 */

#pragma once

#include "game_backend.h"
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

/** Directory name for ring backup storage (relative to tree_root). */
#define PRAXIS_RING_DIR_NAME L".praxis_ring"

/** Filename for last restore metadata (stored in PRAXIS_RING_DIR_NAME). */
#define PRAXIS_RING_LAST_RESTORE_FILE L"last_restore.txt"

bool ring_backup_init(const wchar_t *tree_root, int ring_size);
bool ring_backup_snapshot(const game_backend_t *backend, const wchar_t *current_save,
                          const wchar_t *operation_label, int compression_level,
                          wchar_t *out_backup_path, size_t out_chars);
bool ring_backup_get_latest(const wchar_t *tree_root, wchar_t *out_path, size_t out_chars);
