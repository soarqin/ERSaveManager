/**
 * @file praxis_hotkey_actions.c
 * @brief Hotkey-triggered backup/restore action implementations.
 * @details Provides the four core operations (backup-full, backup-slot,
 *          restore, undo) invoked from both global hotkeys and toolbar buttons.
 *          All functions are stateless with respect to global variables: they
 *          receive the profile store and save-tree widget as parameters.
 */

#include "praxis_hotkey_actions.h"
#include "config.h"
#include "backend_registry.h"
#include "ring_backup.h"
#include "restore_safe.h"
#include "save_compress.h"

#include <stdint.h>
#include <wchar.h>

#include <windows.h>
#include <shlwapi.h>

/* Resolve the active game backend from a profile store. Mirrors the
 * get_active_backend() helper in main.c but operates on an explicit store
 * pointer rather than the global g_profile_store. */
static const game_backend_t *get_active_backend_for(const profile_store_t *store) {
    const game_profile_t *gp = NULL;
    const backup_profile_t *bp = profile_store_get_active_backup(store);

    if (bp) {
        gp = profile_store_find_game_by_id(store, bp->parent_game_id);
    }

    if (!gp) {
        gp = profile_store_get_active_game(store);
    }

    if (gp) {
        const game_backend_t *backend = backend_registry_get_by_id(gp->game_id);
        if (backend) {
            return backend;
        }
    }

    return backend_registry_get_default();
}

/* Resolve the active save file path. Uses the game profile's original_save_dir
 * override when set; otherwise asks the backend to discover it. */
static bool resolve_save_path_for(const profile_store_t *store,
                                  wchar_t *out, size_t out_chars) {
    const backup_profile_t *bp = profile_store_get_active_backup(store);
    const game_profile_t *gp = NULL;
    const game_backend_t *backend = get_active_backend_for(store);

    if (bp) {
        gp = profile_store_find_game_by_id(store, bp->parent_game_id);
    }

    if (!gp) {
        gp = profile_store_get_active_game(store);
    }

    if (!out || out_chars == 0 || !bp || !gp || !backend) {
        return false;
    }

    if (gp->original_save_dir[0] != L'\0') {
        lstrcpynW(out, gp->original_save_dir, (int)out_chars);
        return PathAppendW(out, L"ER0000.sl2") == TRUE;
    }

    return backend->resolve_save_path(out, out_chars);
}

/* Map a compression_level_t enum value to an LZMA compression level integer. */
static int comp_level_to_lzma(compression_level_t cl) {
    switch (cl) {
    case COMP_LEVEL_HIGH:   return ERSM_LEVEL_MAX;    /* 9 */
    case COMP_LEVEL_MEDIUM: return ERSM_LEVEL_NORMAL; /* 5 */
    case COMP_LEVEL_LOW:    return ERSM_LEVEL_FAST;   /* 1 */
    case COMP_LEVEL_NONE:   return ERSM_LEVEL_FAST;   /* 1 — slot saves; full saves use raw BND4 */
    default:                return ERSM_LEVEL_FAST;
    }
}

/* Build a timestamped backup filename: <base_dir>\<prefix>_YYYYMMDD_HHMMSS<ext>. */
static void make_backup_filename(const wchar_t *base_dir, const wchar_t *prefix,
    const wchar_t *ext, wchar_t *out, size_t out_chars) {
    SYSTEMTIME st;

    if (!base_dir || !prefix || !ext || !out || out_chars == 0) {
        return;
    }

    GetLocalTime(&st);
    _snwprintf_s(out, out_chars, _TRUNCATE,
        L"%ls\\%ls_%04d%02d%02d_%02d%02d%02d%ls",
        base_dir, prefix,
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, ext);
}

/* Copy a raw BND4 save file to dst_path, wrapping it with the ERSM
 * raw-BND4 container so it round-trips through save_compress_classify_backup. */
static bool backup_full_raw(const wchar_t *src_path, const wchar_t *dst_path) {
    HANDLE file;
    DWORD file_size;
    uint8_t *buf;
    DWORD bytes_read = 0;
    bool ok;

    if (!src_path || !dst_path) {
        return false;
    }

    file = CreateFileW(src_path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    file_size = GetFileSize(file, NULL);
    if (file_size == INVALID_FILE_SIZE || file_size < 4) {
        CloseHandle(file);
        return false;
    }

    buf = (uint8_t *)LocalAlloc(LMEM_FIXED, file_size);
    if (!buf) {
        CloseHandle(file);
        return false;
    }

    ok = ReadFile(file, buf, file_size, &bytes_read, NULL) && bytes_read == file_size;
    CloseHandle(file);
    if (!ok) {
        LocalFree(buf);
        return false;
    }

    ok = ersm_write_raw_bnd4_to_file(dst_path, buf, (size_t)bytes_read);
    LocalFree(buf);
    return ok;
}

/*
 * Backup race-condition note:
 *
 * When a backup is created, the filesystem watcher worker thread detects the
 * change and posts WM_APP+1 to the UI thread. This sets a 200ms debounce
 * timer; on expiry, save_tree_refresh_preserve_selection() runs.
 *
 * Sequence for praxis_hotkey_action_backup_full() / praxis_hotkey_action_backup_slot():
 *   T0:    WM_COMMAND IDC_BTN_BACKUP_* dispatched, this function begins
 *   T0+a:  Backup file created on disk
 *   T0+b:  Worker thread posts WM_APP+1 (queued; this function does not pump messages)
 *   T0+c:  save_tree_refresh() rebuilds items[] -- new file present
 *   T0+d:  save_tree_select_full_path() sets selection on new file
 *   T0+e:  Function returns
 *   T0+f:  Message loop dispatches WM_APP+1 -> SetTimer(IDT_REFRESH_DEBOUNCE, 200)
 *   T0+200ms: WM_TIMER -> save_tree_refresh_preserve_selection()
 *           - Captures saved_relpath = our newly-set path (the new file)
 *           - Refreshes (file still exists)
 *           - Walk-up: exact match succeeds -> re-selects same file
 *
 * Conclusion: because the UI thread is single-threaded and our manual
 * refresh+select runs to completion before any pending WM_APP+1 is dispatched,
 * the watcher's later refresh sees our selection and preserves it via
 * exact-match walk-up. No race.
 */

bool praxis_hotkey_action_backup_full(HWND hwnd, profile_store_t *store,
                                      save_tree_t *save_tree, int compression_level) {
    const backup_profile_t *bp = profile_store_get_active_backup(store);
    const game_backend_t *backend = get_active_backend_for(store);
    compression_level_t cl = (compression_level_t)compression_level;
    wchar_t save_path[MAX_PATH];
    wchar_t dst[MAX_PATH];
    wchar_t base_dir[MAX_PATH];
    const wchar_t *ext;
    bool ok;

    (void)hwnd;

    if (!bp || !backend || !resolve_save_path_for(store, save_path, MAX_PATH)) {
        return false;
    }

    if (!save_tree || !save_tree_get_selected_dir(save_tree, base_dir, MAX_PATH)) {
        if (!profile_store_resolve_backup_root(store, bp->id, base_dir, MAX_PATH)) {
            return false;
        }
    }

    ext = (cl == COMP_LEVEL_NONE) ? L".sl2" : L".ersm";
    make_backup_filename(base_dir, L"manual", ext, dst, MAX_PATH);

    if (cl == COMP_LEVEL_NONE) {
        ok = backup_full_raw(save_path, dst);
    } else {
        ok = backend->backup_full(save_path, dst, comp_level_to_lzma(cl));
    }

    if (ok && save_tree) {
        save_tree_refresh(save_tree);
        save_tree_select_full_path(save_tree, dst);
    }

    return ok;
}

bool praxis_hotkey_action_backup_slot(HWND hwnd, profile_store_t *store,
                                      save_tree_t *save_tree, int compression_level) {
    const backup_profile_t *bp = profile_store_get_active_backup(store);
    const game_backend_t *backend = get_active_backend_for(store);
    wchar_t save_path[MAX_PATH];
    wchar_t dst[MAX_PATH];
    wchar_t base_dir[MAX_PATH];
    wchar_t prefix[32];
    int slot = -1;
    bool ok;

    (void)hwnd;

    if (!bp || !game_backend_supports_slot_ops(backend) ||
        !resolve_save_path_for(store, save_path, MAX_PATH) ||
        !backend->get_active_slot(save_path, &slot)) {
        return false;
    }

    if (!save_tree || !save_tree_get_selected_dir(save_tree, base_dir, MAX_PATH)) {
        if (!profile_store_resolve_backup_root(store, bp->id, base_dir, MAX_PATH)) {
            return false;
        }
    }

    _snwprintf_s(prefix, 32, _TRUNCATE, L"slot%d_backup", slot);
    make_backup_filename(base_dir, prefix, L".ersm", dst, MAX_PATH);
    ok = backend->backup_slot(save_path, slot, dst,
        comp_level_to_lzma((compression_level_t)compression_level));
    if (ok && save_tree) {
        save_tree_refresh(save_tree);
        save_tree_select_full_path(save_tree, dst);
    }

    return ok;
}

bool praxis_hotkey_action_restore(HWND hwnd, profile_store_t *store, save_tree_t *save_tree) {
    const backup_profile_t *bp = profile_store_get_active_backup(store);
    const game_backend_t *backend = get_active_backend_for(store);
    wchar_t selected_path[MAX_PATH] = {0};
    wchar_t save_path[MAX_PATH];
    wchar_t backup_root[MAX_PATH];
    bool ok;

    (void)hwnd;

    if (!bp || !backend || !save_tree ||
        !save_tree_get_selected_path(save_tree, selected_path, MAX_PATH) || !selected_path[0] ||
        !resolve_save_path_for(store, save_path, MAX_PATH) ||
        !profile_store_resolve_backup_root(store, bp->id, backup_root, MAX_PATH) ||
        !ring_backup_init(backup_root, praxis_config.ring_size)) {
        return false;
    }

    ok = restore_safe_auto(backend, selected_path, save_path, backup_root,
        comp_level_to_lzma(bp->compression_level));
    if (ok && save_tree) {
        save_tree_refresh(save_tree);
    }

    return ok;
}

bool praxis_hotkey_action_undo(HWND hwnd, profile_store_t *store) {
    const backup_profile_t *bp = profile_store_get_active_backup(store);
    const game_backend_t *backend = get_active_backend_for(store);
    wchar_t backup_root[MAX_PATH];
    bool ok;

    (void)hwnd;

    if (!bp || !backend ||
        !profile_store_resolve_backup_root(store, bp->id, backup_root, MAX_PATH) ||
        !ring_backup_init(backup_root, praxis_config.ring_size)) {
        return false;
    }

    ok = restore_safe_undo(backend, backup_root,
        comp_level_to_lzma(bp->compression_level));
    return ok;
}
