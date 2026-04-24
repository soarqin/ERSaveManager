/**
 * @file restore_safe.c
 * @brief Safe restore with ring backup and undo support.
 */

#include "restore_safe.h"
#include "ring_backup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <shlwapi.h>

static bool write_last_restore_meta(const wchar_t *tree_root, const wchar_t *ring_path,
                                    const wchar_t *save_path, bool slot_mode, int slot_index) {
    wchar_t meta_path[MAX_PATH];
    lstrcpyW(meta_path, tree_root);
    PathAppendW(meta_path, L".praxis_ring");
    PathAppendW(meta_path, L"last_restore.txt");

    /* Format: ring_path|save_path|slot_mode|slot_index */
    char buf[MAX_PATH * 8 + 32];
    char ring_utf8[MAX_PATH * 4];
    char save_utf8[MAX_PATH * 4];
    WideCharToMultiByte(CP_UTF8, 0, ring_path, -1, ring_utf8, sizeof(ring_utf8), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, save_path, -1, save_utf8, sizeof(save_utf8), NULL, NULL);
    int len = _snprintf(buf, sizeof(buf), "%s|%s|%d|%d\n", ring_utf8, save_utf8, slot_mode ? 1 : 0, slot_index);
    if (len <= 0) return false;

    HANDLE fh = CreateFileW(meta_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) return false;
    DWORD written;
    bool ok = WriteFile(fh, buf, (DWORD)len, &written, NULL) && written == (DWORD)len;
    CloseHandle(fh);
    return ok;
}

static bool read_last_restore_meta(const wchar_t *tree_root, wchar_t *out_ring_path,
                                   size_t ring_chars, wchar_t *out_save_path, size_t save_chars,
                                   bool *out_slot_mode, int *out_slot_index) {
    wchar_t meta_path[MAX_PATH];
    lstrcpyW(meta_path, tree_root);
    PathAppendW(meta_path, L".praxis_ring");
    PathAppendW(meta_path, L"last_restore.txt");

    HANDLE fh = CreateFileW(meta_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) return false;

    char buf[MAX_PATH * 8 + 32];
    DWORD bytes_read;
    if (!ReadFile(fh, buf, sizeof(buf) - 1, &bytes_read, NULL)) { CloseHandle(fh); return false; }
    buf[bytes_read] = '\0';
    CloseHandle(fh);

    /* Parse: ring_path|save_path|slot_mode|slot_index */
    char *pipe1 = strchr(buf, '|');
    if (!pipe1) return false;
    *pipe1 = '\0';
    char *pipe2 = strchr(pipe1 + 1, '|');
    if (!pipe2) return false;
    *pipe2 = '\0';
    char *pipe3 = strchr(pipe2 + 1, '|');
    if (!pipe3) return false;
    *pipe3 = '\0';

    MultiByteToWideChar(CP_UTF8, 0, buf, -1, out_ring_path, (int)ring_chars);
    MultiByteToWideChar(CP_UTF8, 0, pipe1 + 1, -1, out_save_path, (int)save_chars);
    *out_slot_mode = atoi(pipe2 + 1) != 0;
    *out_slot_index = atoi(pipe3 + 1);
    return true;
}

bool restore_with_safety(const game_backend_t *backend,
                         const wchar_t *backup_src,
                         const wchar_t *save_dst,
                         const wchar_t *tree_root,
                         int compression_level,
                         bool slot_mode,
                         int slot_index) {
    if (!backend || !backup_src || !save_dst || !tree_root) return false;

    /* Step 1: Ring snapshot of current save */
    const wchar_t *label = slot_mode ? L"pre_slot_restore" : L"pre_full_restore";
    wchar_t ring_path[MAX_PATH];
    if (!ring_backup_snapshot(backend, save_dst, label, compression_level, ring_path, MAX_PATH)) {
        return false; /* Abort restore if ring fails */
    }

    /* Step 2: Perform restore */
    bool ok;
    if (slot_mode && backend->restore_slot) {
        ok = backend->restore_slot(backup_src, save_dst, slot_index);
    } else {
        ok = backend->restore_full(backup_src, save_dst);
    }
    if (!ok) return false;

    /* Step 3: Write last_restore metadata (save_path lets undo target the same file) */
    write_last_restore_meta(tree_root, ring_path, save_dst, slot_mode, slot_index);
    return true;
}

bool undo_last_restore(const game_backend_t *backend, const wchar_t *tree_root, int compression_level) {
    if (!backend || !tree_root) return false;

    /* Read last restore metadata (ring path + save path + slot info) */
    wchar_t ring_path[MAX_PATH];
    wchar_t save_path[MAX_PATH];
    bool slot_mode;
    int slot_index;
    if (!read_last_restore_meta(tree_root, ring_path, MAX_PATH, save_path, MAX_PATH,
                                &slot_mode, &slot_index)) return false;

    /* Undo IS a restore — so it also snapshots first (enables redo) */
    return restore_with_safety(backend, ring_path, save_path, tree_root, compression_level, slot_mode, slot_index);
}
