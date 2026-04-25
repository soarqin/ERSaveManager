/**
 * @file main.c
 * @brief Praxis application entry point and main window procedure.
 */

#include "config.h"
#include "config_core.h"
#include "backend_registry.h"
#include "hotkey.h"
#include "locale.h"
#include "resource.h"
#include "file_dialog.h"
#include "ring_backup.h"
#include "restore_safe.h"
#include "save_tree.h"
#include "save_watcher.h"
#include "profile_store.h"
#include "toolbar.h"
#include "dialogs/edit_game_profile.h"
#include "dialogs/game_profile_manager.h"
#include "dialogs/edit_backup_profile.h"

#include "ersave.h"
#include "save_compress.h"

#include <md5.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <uxtheme.h>

/** @brief Global main window handle (set on WM_CREATE). */
static HWND g_main_window = NULL;
static save_tree_t *g_save_tree = NULL;
static save_watcher_t *g_save_watcher = NULL;
static HWND g_status_bar = NULL;
static toolbar_t *g_toolbar = NULL;
static profile_store_t g_profile_store;

/** @brief Log file handle opened via --log-file flag (for Gate I testing). */
static HANDLE g_log_file = INVALID_HANDLE_VALUE;

/* Forward declarations */
static LRESULT CALLBACK praxis_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static int run_selftest(void);

#define IDT_REFRESH_DEBOUNCE 1001
#define WM_WATCHER_NOTIFY (WM_APP + 1)

/* Write a UTF-8 encoded line to the log file if one is open. */
static void log_write(const wchar_t *msg) {
    if (g_log_file == INVALID_HANDLE_VALUE) return;
    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, msg, -1, NULL, 0, NULL, NULL);
    if (utf8_size <= 0) return;
    char *utf8 = LocalAlloc(LMEM_FIXED, utf8_size);
    if (!utf8) return;
    WideCharToMultiByte(CP_UTF8, 0, msg, -1, utf8, utf8_size, NULL, NULL);
    DWORD written;
    WriteFile(g_log_file, utf8, (DWORD)(utf8_size - 1), &written, NULL);
    LocalFree(utf8);
}

/* Formatted wide-char printf honoring stdout redirect set by the parent process. */
static void st_printf(const wchar_t *fmt, ...) {
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf(buf, 1024, fmt, args);
    va_end(args);
    buf[1023] = L'\0';
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!hOut || hOut == INVALID_HANDLE_VALUE) return;
    DWORD type = GetFileType(hOut);
    DWORD written;
    if (type == FILE_TYPE_CHAR) {
        WriteConsoleW(hOut, buf, (DWORD)wcslen(buf), &written, NULL);
    } else {
        int utf8_size = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
        if (utf8_size <= 0) return;
        char *utf8 = LocalAlloc(LMEM_FIXED, utf8_size);
        if (!utf8) return;
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, utf8_size, NULL, NULL);
        WriteFile(hOut, utf8, (DWORD)(utf8_size - 1), &written, NULL);
        LocalFree(utf8);
    }
}

static const game_backend_t *get_active_backend(void) {
    const game_profile_t *gp = NULL;
    const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);

    if (bp) {
        for (size_t i = 0; i < g_profile_store.game_count; i++) {
            if (g_profile_store.games[i].id == bp->parent_game_id) {
                gp = &g_profile_store.games[i];
                break;
            }
        }
    }

    if (!gp) {
        gp = profile_store_get_active_game(&g_profile_store);
    }

    if (gp) {
        const game_backend_t *backend = backend_registry_get_by_id(gp->game_id);
        if (backend) {
            return backend;
        }
    }

    return backend_registry_get_default();
}

static bool save_profile_store(void) {
    wchar_t ini[MAX_PATH];

    if (!config_core_get_app_ini_path(ini, MAX_PATH, L"Praxis.ini")) {
        return false;
    }

    return profile_store_save(&g_profile_store, ini);
}

static int comp_level_to_lzma(compression_level_t cl) {
    switch (cl) {
    case COMP_LEVEL_HIGH:   return ERSM_LEVEL_MAX;    /* 9 */
    case COMP_LEVEL_MEDIUM: return ERSM_LEVEL_NORMAL; /* 5 */
    case COMP_LEVEL_LOW:    return ERSM_LEVEL_FAST;   /* 1 */
    case COMP_LEVEL_NONE:   return ERSM_LEVEL_FAST;   /* 1 — slot saves; full saves use raw BND4 */
    default:                return ERSM_LEVEL_FAST;
    }
}

static bool resolve_save_path_for_active(wchar_t *out, size_t out_chars) {
    const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);
    const game_profile_t *gp = NULL;
    const game_backend_t *backend = get_active_backend();

    if (bp) {
        for (size_t i = 0; i < g_profile_store.game_count; i++) {
            if (g_profile_store.games[i].id == bp->parent_game_id) {
                gp = &g_profile_store.games[i];
                break;
            }
        }
    }

    if (!gp) {
        gp = profile_store_get_active_game(&g_profile_store);
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

static void set_active_status_text(void) {
    const game_profile_t *gp = NULL;
    const backup_profile_t *bp;
    wchar_t status[256];

    if (!g_status_bar) {
        return;
    }

    bp = profile_store_get_active_backup(&g_profile_store);
    if (bp) {
        for (size_t i = 0; i < g_profile_store.game_count; i++) {
            if (g_profile_store.games[i].id == bp->parent_game_id) {
                gp = &g_profile_store.games[i];
                break;
            }
        }
    }
    if (!gp) {
        gp = profile_store_get_active_game(&g_profile_store);
    }
    if (gp && bp) {
        _snwprintf_s(status, 256, _TRUNCATE, L"Active: %ls / %ls", gp->name, bp->name);
        SetWindowTextW(g_status_bar, status);
        return;
    }

    SetWindowTextW(g_status_bar, praxis_locale_str(STR_PRAXIS_APP_TITLE));
}

static void apply_active_profile_ui(HWND hwnd) {
    const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);
    wchar_t backup_root[MAX_PATH];

    if (g_toolbar) {
        toolbar_set_selected_backup_id(g_toolbar, bp ? bp->id : 0);
        toolbar_set_actions_enabled(g_toolbar, bp != NULL);
    }

    if (bp == NULL ||
        !profile_store_resolve_backup_root(&g_profile_store, bp->id, backup_root, MAX_PATH)) {
        set_active_status_text();
        return;
    }

    if (g_save_tree) {
        save_tree_set_root(g_save_tree, backup_root);
    }

    if (g_save_watcher) {
        save_watcher_change_root(g_save_watcher, backup_root);
    } else {
        g_save_watcher = save_watcher_start(hwnd, backup_root, WM_WATCHER_NOTIFY);
    }

    set_active_status_text();
}

/*
 * Backup race-condition note:
 *
 * When a backup is created, the filesystem watcher worker thread detects the
 * change and posts WM_APP+1 to the UI thread. This sets a 200ms debounce
 * timer; on expiry, save_tree_refresh_preserve_selection() runs.
 *
 * Sequence for backup_full_active() / backup_slot_active():
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

static bool backup_full_active(void) {
    const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);
    const game_backend_t *backend = get_active_backend();
    wchar_t save_path[MAX_PATH];
    wchar_t dst[MAX_PATH];
    wchar_t base_dir[MAX_PATH];
    const wchar_t *ext;
    bool ok;

    if (!bp || !backend || !resolve_save_path_for_active(save_path, MAX_PATH)) {
        return false;
    }

    if (!g_save_tree || !save_tree_get_selected_dir(g_save_tree, base_dir, MAX_PATH)) {
        if (!profile_store_resolve_backup_root(&g_profile_store, bp->id, base_dir, MAX_PATH)) {
            return false;
        }
    }

    ext = (bp->compression_level == COMP_LEVEL_NONE) ? L".sl2" : L".ersm";
    make_backup_filename(base_dir, L"manual", ext, dst, MAX_PATH);

    if (bp->compression_level == COMP_LEVEL_NONE) {
        ok = backup_full_raw(save_path, dst);
    } else {
        ok = backend->backup_full(save_path, dst, comp_level_to_lzma(bp->compression_level));
    }

    if (ok && g_save_tree) {
        save_tree_refresh(g_save_tree);
        save_tree_select_full_path(g_save_tree, dst);
    }

    return ok;
}

static bool backup_slot_active(void) {
    const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);
    const game_backend_t *backend = get_active_backend();
    wchar_t save_path[MAX_PATH];
    wchar_t dst[MAX_PATH];
    wchar_t base_dir[MAX_PATH];
    wchar_t prefix[32];
    int slot = -1;
    bool ok;

    if (!bp || !game_backend_supports_slot_ops(backend) ||
        !resolve_save_path_for_active(save_path, MAX_PATH) ||
        !backend->get_active_slot(save_path, &slot)) {
        return false;
    }

    if (!g_save_tree || !save_tree_get_selected_dir(g_save_tree, base_dir, MAX_PATH)) {
        if (!profile_store_resolve_backup_root(&g_profile_store, bp->id, base_dir, MAX_PATH)) {
            return false;
        }
    }

    _snwprintf_s(prefix, 32, _TRUNCATE, L"slot%d_backup", slot);
    make_backup_filename(base_dir, prefix, L".ersm", dst, MAX_PATH);
    ok = backend->backup_slot(save_path, slot, dst, comp_level_to_lzma(bp->compression_level));
    if (ok && g_save_tree) {
        save_tree_refresh(g_save_tree);
        save_tree_select_full_path(g_save_tree, dst);
    }

    return ok;
}

static bool restore_active_selection(void) {
    const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);
    const game_backend_t *backend = get_active_backend();
    wchar_t selected_path[MAX_PATH] = {0};
    wchar_t save_path[MAX_PATH];
    wchar_t backup_root[MAX_PATH];
    bool ok;

    if (!bp || !backend || !g_save_tree ||
        !save_tree_get_selected_path(g_save_tree, selected_path, MAX_PATH) || !selected_path[0] ||
        !resolve_save_path_for_active(save_path, MAX_PATH) ||
        !profile_store_resolve_backup_root(&g_profile_store, bp->id, backup_root, MAX_PATH) ||
        !ring_backup_init(backup_root, praxis_config.ring_size)) {
        return false;
    }

    ok = restore_with_safety_auto(backend, selected_path, save_path, backup_root,
        comp_level_to_lzma(bp->compression_level));
    if (ok && g_save_tree) {
        save_tree_refresh(g_save_tree);
    }

    return ok;
}

static bool undo_active_restore(void) {
    const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);
    const game_backend_t *backend = get_active_backend();
    wchar_t backup_root[MAX_PATH];
    bool ok;

    if (!bp || !backend ||
        !profile_store_resolve_backup_root(&g_profile_store, bp->id, backup_root, MAX_PATH) ||
        !ring_backup_init(backup_root, praxis_config.ring_size)) {
        return false;
    }

    ok = undo_last_restore(backend, backup_root, comp_level_to_lzma(bp->compression_level));
    if (ok && g_save_tree) {
        save_tree_refresh(g_save_tree);
    }

    return ok;
}

static LRESULT CALLBACK praxis_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        {
            CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
            hotkey_binding_t b;

            g_save_tree = save_tree_create(hwnd, cs->hInstance, IDC_TREE_VIEW);
            if (!g_save_tree) {
                return -1;
            }

            g_toolbar = toolbar_create(hwnd, cs->hInstance);
            if (g_toolbar) {
                RECT client_rect;

                if (GetClientRect(hwnd, &client_rect)) {
                    int cw = client_rect.right - client_rect.left;
                    int top_h = toolbar_get_top_height(g_toolbar);
                    /* Initial layout — WM_SIZE will refine y_top once the
                     * status bar is also created and measured. */
                    toolbar_layout_top(g_toolbar, cw);
                    toolbar_layout_bottom(g_toolbar, cw, top_h);
                }
            }

            g_status_bar = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                0, 0, 0, 0, hwnd, (HMENU)(uintptr_t)IDC_STATUS_BAR, cs->hInstance, NULL);
            if (!g_status_bar) {
                if (g_toolbar) {
                    toolbar_destroy(g_toolbar);
                    g_toolbar = NULL;
                }
                save_tree_destroy(g_save_tree);
                g_save_tree = NULL;
                return -1;
            }

            save_tree_set_root(g_save_tree, praxis_config.tree_root);

            if (g_save_tree && praxis_config.tree_root[0] != L'\0') {
                g_save_watcher = save_watcher_start(hwnd, praxis_config.tree_root, WM_WATCHER_NOTIFY);
            }

            /* Load profile store and populate toolbar combobox. */
            profile_store_init(&g_profile_store);
            {
                wchar_t ini[MAX_PATH];
                if (config_core_get_app_ini_path(ini, MAX_PATH, L"Praxis.ini")) {
                    profile_store_load(&g_profile_store, ini);
                }
                if (g_toolbar) toolbar_populate_profiles(g_toolbar, &g_profile_store);
            }

            apply_active_profile_ui(hwnd);

            g_main_window = hwnd;

            hotkey_init(hwnd);
            if (hotkey_parse_string(praxis_config.hotkey_backup_full, &b))
                hotkey_register(HOTKEY_BACKUP_FULL, &b);
            if (hotkey_parse_string(praxis_config.hotkey_backup_slot, &b))
                hotkey_register(HOTKEY_BACKUP_SLOT, &b);
            if (hotkey_parse_string(praxis_config.hotkey_restore, &b))
                hotkey_register(HOTKEY_RESTORE, &b);
            if (hotkey_parse_string(praxis_config.hotkey_undo_restore, &b))
                hotkey_register(HOTKEY_UNDO_RESTORE, &b);

        }
        return 0;

    case WM_SIZE: {
        int client_width = (int)LOWORD(lp);
        int client_height = (int)HIWORD(lp);

        int top_h    = g_toolbar ? toolbar_get_top_height(g_toolbar)    : 0;
        int bottom_h = g_toolbar ? toolbar_get_bottom_height(g_toolbar) : 0;

        /* Top toolbar at y=0 */
        if (g_toolbar) {
            toolbar_layout_top(g_toolbar, client_width);
        }

        /* Status bar at very bottom (auto-positions itself) */
        int status_height = 0;
        if (g_status_bar) {
            SendMessageW(g_status_bar, WM_SIZE, wp, lp);  /* let status bar resize itself */
            RECT sr;
            GetWindowRect(g_status_bar, &sr);
            status_height = sr.bottom - sr.top;
        }

        /* Bottom toolbar above status bar */
        int bottom_y = client_height - status_height - bottom_h;
        if (bottom_y < top_h) bottom_y = top_h;
        if (g_toolbar) {
            toolbar_layout_bottom(g_toolbar, client_width, bottom_y);
        }

        /* TreeView fills middle: from top_h down to bottom_y */
        if (g_save_tree) {
            HWND htree = save_tree_get_hwnd(g_save_tree);
            if (htree) {
                int tree_h = bottom_y - top_h;
                if (tree_h < 0) tree_h = 0;
                MoveWindow(htree, 0, top_h, client_width, tree_h, TRUE);
            }
        }
        return 0;
    }

    case WM_NOTIFY:
        if (g_save_tree) {
            LRESULT notify_result = 0;

            if (save_tree_handle_notify(g_save_tree, (LPNMHDR)lp, &notify_result)) {
                return notify_result;
            }
        }
        break;

    case WM_WATCHER_NOTIFY:
        if (wp != 2) {
            SetTimer(hwnd, IDT_REFRESH_DEBOUNCE, 200, NULL);
        }
        return 0;

    case WM_TIMER:
        if (wp == IDT_REFRESH_DEBOUNCE) {
            KillTimer(hwnd, IDT_REFRESH_DEBOUNCE);
            if (g_save_tree) {
                save_tree_refresh_preserve_selection(g_save_tree);
            }
        }
        return 0;

    case WM_COMMAND:
        /* Dynamic Game submenu profile selection */
        if (LOWORD(wp) >= IDM_GAME_PROFILE_FIRST && LOWORD(wp) <= IDM_GAME_PROFILE_LAST) {
            int game_id = (int)(LOWORD(wp) - IDM_GAME_PROFILE_FIRST);
            const backup_profile_t *backups[1] = {0};

            profile_store_set_active_game(&g_profile_store, game_id);
            if (profile_store_list_backups_for_game(&g_profile_store, game_id, backups, 1) > 0 && backups[0]) {
                profile_store_set_active_backup(&g_profile_store, backups[0]->id);
            } else {
                g_profile_store.active_backup_id = 0;
            }
            /* Save updated active game */
            save_profile_store();
            /* Repopulate toolbar with backups for new active game */
            if (g_toolbar) toolbar_populate_profiles(g_toolbar, &g_profile_store);
            apply_active_profile_ui(hwnd);
            return 0;
        }
        if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == IDC_PROFILE_COMBO) {
            int selected_id = toolbar_get_selected_backup_id(g_toolbar);

            if (selected_id > 0 && profile_store_set_active_backup(&g_profile_store, selected_id)) {
                const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);
                if (bp) {
                    profile_store_set_active_game(&g_profile_store, bp->parent_game_id);
                }
                save_profile_store();
            }

            apply_active_profile_ui(hwnd);
            return 0;
        }
        switch (LOWORD(wp)) {
        case IDM_GAME_MANAGE:
            {
                wchar_t ini[MAX_PATH];
                config_core_get_app_ini_path(ini, MAX_PATH, L"Praxis.ini");
                show_game_profile_manager(hwnd, &g_profile_store, ini);
                /* Repopulate toolbar combobox */
                if (g_toolbar) toolbar_populate_profiles(g_toolbar, &g_profile_store);
                apply_active_profile_ui(hwnd);
            }
            return 0;
        case IDC_BTN_BACKUP_FULL:
            backup_full_active();
            return 0;
        case IDC_BTN_BACKUP_SLOT:
            backup_slot_active();
            return 0;
        case IDC_BTN_RESTORE:
            restore_active_selection();
            return 0;
        case IDC_BTN_UNDO:
            undo_active_restore();
            return 0;
        case IDC_BTN_ADD_BACKUP:
            {
                const game_profile_t *gp = profile_store_get_active_game(&g_profile_store);

                if (!gp) {
                    return 0;
                }

                backup_profile_t new_bp;
                int new_backup_id;
                ZeroMemory(&new_bp, sizeof(new_bp));
                new_bp.parent_game_id = gp->id;
                new_bp.compression_level = COMP_LEVEL_MEDIUM;

                if (edit_backup_profile(hwnd, &new_bp, true) != IDOK) {
                    return 0;
                }

                new_backup_id = profile_store_add_backup(&g_profile_store, &new_bp);
                if (new_backup_id != 0) {
                    profile_store_set_active_backup(&g_profile_store, new_backup_id);
                    save_profile_store();
                    if (g_toolbar) {
                        toolbar_populate_profiles(g_toolbar, &g_profile_store);
                    }
                    apply_active_profile_ui(hwnd);
                }
            }
            return 0;
        case IDC_BTN_DEL_BACKUP:
            {
                int backup_id = toolbar_get_selected_backup_id(g_toolbar);
                int response;

                if (backup_id == 0) {
                    return 0;
                }

                response = MessageBoxW(hwnd, L"Delete this backup profile?", L"Confirm",
                    MB_YESNO | MB_ICONQUESTION);
                if (response != IDYES) {
                    return 0;
                }

                if (profile_store_delete_backup(&g_profile_store, backup_id)) {
                    save_profile_store();
                    if (g_toolbar) {
                        toolbar_populate_profiles(g_toolbar, &g_profile_store);
                    }
                    apply_active_profile_ui(hwnd);
                }
            }
            return 0;
        case IDM_FILE_EXIT:
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        case IDM_OPTIONS_HOTKEYS:
            MessageBoxW(hwnd, L"Configure hotkeys in Praxis.ini", L"Hotkey Settings", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        return 0;

    case WM_INITMENUPOPUP: {
            HMENU sub = (HMENU)wp;
            /* Identify Game submenu by checking if it contains IDM_GAME_MANAGE */
            int count = GetMenuItemCount(sub);
            bool is_game_menu = false;
            for (int i = 0; i < count; i++) {
                if (GetMenuItemID(sub, i) == IDM_GAME_MANAGE) { is_game_menu = true; break; }
            }
            if (is_game_menu) {
                /* Keep ONLY the Manage item (1 item). Remove all dynamically inserted items. */
                while (GetMenuItemCount(sub) > 1) {
                    DeleteMenu(sub, 0, MF_BYPOSITION);
                }
                /* Insert game profiles at top */
                for (int i = 0; i < (int)g_profile_store.game_count; i++) {
                    UINT flags = MF_BYPOSITION | MF_STRING;
                    if (g_profile_store.games[i].id == g_profile_store.active_game_id) {
                        flags |= MF_CHECKED;
                    }
                    InsertMenuW(sub, i, flags,
                                IDM_GAME_PROFILE_FIRST + g_profile_store.games[i].id,
                                g_profile_store.games[i].name);
                }
                /* Insert separator BEFORE the Manage item only when there ARE profiles. */
                if (g_profile_store.game_count > 0) {
                    InsertMenuW(sub, (int)g_profile_store.game_count, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
                }
            }
            return 0;
        }

    case WM_HOTKEY: {
            wchar_t log_msg[64];
            const game_backend_t *backend = get_active_backend();

            _snwprintf(log_msg, 64, L"HOTKEY_FIRED id=%d\n", (int)wp);
            log_msg[63] = L'\0';
            log_write(log_msg);

            if (!backend) return 0;

            switch ((hotkey_id_t)wp) {
            case HOTKEY_BACKUP_FULL:
                backup_full_active();
                break;
            case HOTKEY_BACKUP_SLOT:
                backup_slot_active();
                break;
            case HOTKEY_RESTORE:
                restore_active_selection();
                break;
            case HOTKEY_UNDO_RESTORE:
                undo_active_restore();
                break;
            }
            return 0;
        }

    case WM_CLOSE:
        praxis_config.window_x = -1;
        praxis_config.window_y = -1;
        {
            RECT r;
            if (GetWindowRect(hwnd, &r)) {
                praxis_config.window_x = r.left;
                praxis_config.window_y = r.top;
                praxis_config.window_width = r.right - r.left;
                praxis_config.window_height = r.bottom - r.top;
            }
        }
        praxis_config.language = praxis_locale_get_current();
        /* Persist the full INI (settings + profile sections). Using
         * praxis_save_config() here would have erased every
         * [GameProfile:N]/[BackupProfile:N] block on every shutdown. */
        save_profile_store();
        hotkey_unregister_all();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_save_watcher) {
            save_watcher_stop(g_save_watcher);
            g_save_watcher = NULL;
        }
        KillTimer(hwnd, IDT_REFRESH_DEBOUNCE);
        if (g_toolbar) {
            toolbar_destroy(g_toolbar);
            g_toolbar = NULL;
        }
        save_tree_destroy(g_save_tree);
        g_save_tree = NULL;
        g_status_bar = NULL;
        if (g_log_file != INVALID_HANDLE_VALUE) {
            CloseHandle(g_log_file);
            g_log_file = INVALID_HANDLE_VALUE;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* Creates a minimal valid BND4 save file at path with the given Steam user ID.
 * Mirrors ERSaveManager's make_min_valid_sl2 for headless testing. */
static bool praxis_make_min_valid_sl2(const wchar_t *path, uint64_t user_id) {
    const uint32_t char_slot_size    = 0x280010u;  /* ER_CHAR_SLOT_FILE_SIZE */
    const uint32_t summary_slot_size = 0x60010u;   /* ER_SUMMARY_SLOT_FILE_SIZE */
    const uint32_t slot0_offset      = 0x300u;     /* ER_FILE_HEADER_SIZE */
    const uint32_t summary_data_size = 0x60000u;   /* ER_SUMMARY_DATA_SIZE */
    const uint32_t face_section_size = 0x11D0u;    /* ER_SUMMARY_FACE_SECTION_SIZE */

    const uint32_t summary_offset = slot0_offset + 10u * char_slot_size;
    const uint32_t index_offset   = summary_offset + summary_slot_size;
    const uint32_t total_size     = index_offset + summary_slot_size;
    const uint32_t summary_layout_size = face_section_size + 0x14u;

    uint8_t *file_data = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, total_size);
    if (!file_data) {
        return false;
    }

    /* BND4 magic */
    CopyMemory(file_data, "BND4", 4);
    /* Slot count = 12 */
    *(uint32_t *)(file_data + 0x0C) = 12u;

    /* Slot size + offset arrays: 10 char slots, 1 summary slot, 1 index slot */
    for (int i = 0; i < 10; i++) {
        *(uint32_t *)(file_data + 0x48 + i * 0x20) = char_slot_size;
        *(uint32_t *)(file_data + 0x50 + i * 0x20) = slot0_offset + (uint32_t)i * char_slot_size;
    }
    *(uint32_t *)(file_data + 0x48 + 10 * 0x20) = summary_slot_size;
    *(uint32_t *)(file_data + 0x50 + 10 * 0x20) = summary_offset;
    *(uint32_t *)(file_data + 0x48 + 11 * 0x20) = summary_slot_size;
    *(uint32_t *)(file_data + 0x50 + 11 * 0x20) = index_offset;

    /* Summary payload starts at summary_offset + 0x10 (after MD5 slot header) */
    uint8_t *summary_payload = file_data + summary_offset + 0x10;
    /* user_id at payload offset 0x04 */
    *(uint64_t *)(summary_payload + 0x04) = user_id;
    /* sz field at payload offset 0x150 spans face data, active slot, and padding before availability bytes */
    *(uint32_t *)(summary_payload + 0x150) = summary_layout_size;
    /* face-section size marker at payload offset 0x158 */
    *(uint32_t *)(summary_payload + 0x158) = face_section_size;

    /* MD5 of summary payload bytes goes in the 16-byte slot header prefix */
    md5_buffer(summary_payload, summary_data_size, file_data + summary_offset);

    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        LocalFree(file_data);
        return false;
    }
    DWORD written;
    bool ok = WriteFile(f, file_data, total_size, &written, NULL) && written == total_size;
    CloseHandle(f);
    LocalFree(file_data);
    return ok;
}

static int selftest_make_valid_ersm(const wchar_t *path) {
    uint8_t buf[16] = {0};

    return ersm_compress_to_file(path, buf, sizeof(buf), ERSM_TYPE_CHAR_SLOT, ERSM_LEVEL_NORMAL) ? 0 : 1;
}

static bool selftest_delete_tree_path(const wchar_t *full_path) {
    wchar_t delete_buf[MAX_PATH + 2];
    SHFILEOPSTRUCTW op = {0};

    if (!full_path || full_path[0] == L'\0' || lstrlenW(full_path) >= MAX_PATH + 1) {
        return false;
    }

    lstrcpyW(delete_buf, full_path);
    delete_buf[lstrlenW(delete_buf) + 1] = L'\0';

    op.wFunc = FO_DELETE;
    op.pFrom = delete_buf;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

static HTREEITEM selftest_find_child_by_text(HWND hwnd, HTREEITEM parent, const wchar_t *text) {
    HTREEITEM item;
    wchar_t label[MAX_PATH];

    if (!hwnd || !text) {
        return NULL;
    }

    item = parent ? TreeView_GetChild(hwnd, parent) : TreeView_GetRoot(hwnd);
    while (item) {
        TVITEMW tvi = {0};

        label[0] = L'\0';
        tvi.hItem = item;
        tvi.mask = TVIF_TEXT;
        tvi.pszText = label;
        tvi.cchTextMax = MAX_PATH;
        if (TreeView_GetItem(hwnd, &tvi) && lstrcmpW(label, text) == 0) {
            return item;
        }

        item = TreeView_GetNextSibling(hwnd, item);
    }

    return NULL;
}

static HTREEITEM selftest_find_tree_item_by_relpath(HWND hwnd, const wchar_t *relpath) {
    HTREEITEM item;
    wchar_t path_copy[MAX_PATH];
    wchar_t *context = NULL;
    wchar_t *part;

    if (!hwnd) {
        return NULL;
    }

    item = TreeView_GetRoot(hwnd);
    if (!item || !relpath || relpath[0] == L'\0') {
        return item;
    }

    lstrcpynW(path_copy, relpath, MAX_PATH);
    part = wcstok_s(path_copy, L"\\", &context);
    while (part) {
        item = selftest_find_child_by_text(hwnd, item, part);
        if (!item) {
            return NULL;
        }
        part = wcstok_s(NULL, L"\\", &context);
    }

    return item;
}

static bool selftest_build_tree_path(const wchar_t *root, const wchar_t *relpath, wchar_t *out, size_t out_chars) {
    if (!root || !out || out_chars == 0) {
        return false;
    }

    if ((size_t)lstrlenW(root) >= out_chars) {
        return false;
    }

    lstrcpyW(out, root);
    if (relpath && relpath[0] != L'\0' && !PathAppendW(out, relpath)) {
        return false;
    }

    return true;
}

static void selftest_walk_up_existing_relpath(const wchar_t *root, const wchar_t *start_relpath,
    wchar_t *out_relpath, size_t out_chars) {
    wchar_t try_relpath[MAX_PATH];
    wchar_t full_path[MAX_PATH];

    if (!out_relpath || out_chars == 0) {
        return;
    }

    out_relpath[0] = L'\0';
    if (!root || !start_relpath || start_relpath[0] == L'\0') {
        return;
    }

    lstrcpynW(try_relpath, start_relpath, MAX_PATH);
    while (true) {
        if (selftest_build_tree_path(root, try_relpath, full_path, MAX_PATH) && PathFileExistsW(full_path)) {
            lstrcpynW(out_relpath, try_relpath, out_chars);
            return;
        }

        {
            wchar_t *last_sep = wcsrchr(try_relpath, L'\\');
            if (last_sep) {
                *last_sep = L'\0';
            } else {
                break;
            }
        }
    }
}

static int run_selftest(void) {
    /* Attach or allocate a console when stdout is not already redirected so that
     * test harnesses invoking the WIN32 subsystem binary can capture output. */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD type = hOut ? GetFileType(hOut) : FILE_TYPE_UNKNOWN;
    bool redirected = (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE);
    if (!redirected) {
        AllocConsole();
        FILE *fp = NULL;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
    }

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 3) {
        st_printf(L"usage: --selftest <subcommand> [args...]\n");
        if (argv) LocalFree(argv);
        return 2;
    }

    const wchar_t *sub = argv[2];
    int result;

    if (wcscmp(sub, L"smoke") == 0) {
        /* T14: headless smoke test. Verifies config + LZMA bootstrap succeed
         * without requiring any pre-existing Praxis.ini. */
        praxis_load_config();
        save_compress_init();
        st_printf(L"praxis_smoke_ok\n");
        result = 0;
    } else if (wcscmp(sub, L"dump-default-backend") == 0) {
        const game_backend_t *b = backend_registry_get_default();
        if (!b) {
            st_printf(L"no default backend\n");
            result = 1;
        } else {
            st_printf(L"%ls\n", b->display_name);
            result = 0;
        }
    } else if (wcscmp(sub, L"provision-sl2") == 0) {
        if (argc < 4) {
            st_printf(L"usage: --selftest provision-sl2 <output_path>\n");
            result = 2;
        } else {
            result = praxis_make_min_valid_sl2(argv[3], 76561199999999999ULL) ? 0 : 1;
        }
    } else if (wcscmp(sub, L"backup-full-headless") == 0) {
        if (argc < 5) {
            st_printf(L"usage: --selftest backup-full-headless <src_sl2> <dst_ersm>\n");
            result = 2;
        } else {
            const game_backend_t *b = backend_registry_get_default();
            if (!b) {
                st_printf(L"no backend\n");
                result = 1;
            } else {
                result = b->backup_full(argv[3], argv[4], 5) ? 0 : 1;
            }
        }
    } else if (wcscmp(sub, L"restore-full-headless") == 0) {
        if (argc < 5) {
            st_printf(L"usage: --selftest restore-full-headless <src_backup> <dst_sl2>\n");
            result = 2;
        } else {
            const game_backend_t *b = backend_registry_get_default();
            if (!b) {
                st_printf(L"no backend\n");
                result = 1;
            } else {
                result = b->restore_full(argv[3], argv[4]) ? 0 : 1;
            }
        }
    } else if (wcscmp(sub, L"backup-slot-headless") == 0) {
        if (argc < 6) { result = 2; }
        else {
            const game_backend_t *b = backend_registry_get_default();
            int slot = _wtoi(argv[4]);
            result = (b && b->backup_slot) ? (b->backup_slot(argv[3], slot, argv[5], 5) ? 0 : 1) : 1;
        }
    } else if (wcscmp(sub, L"restore-slot-headless") == 0) {
        if (argc < 6) { result = 2; }
        else {
            const game_backend_t *b = backend_registry_get_default();
            int slot = _wtoi(argv[5]);
            result = (b && b->restore_slot) ? (b->restore_slot(argv[3], argv[4], slot) ? 0 : 1) : 1;
        }
    } else if (wcscmp(sub, L"dump-active-slot-praxis") == 0) {
        if (argc < 4) { result = 2; }
        else {
            const game_backend_t *b = backend_registry_get_default();
            int slot = -1;
            if (b && b->get_active_slot && b->get_active_slot(argv[3], &slot)) {
                st_printf(L"active_slot=%d\n", slot);
                result = 0;
            } else result = 1;
        }
    } else if (wcscmp(sub, L"hotkey-validate") == 0) {
        if (argc < 4) { result = 2; }
        else {
            hotkey_binding_t b;
            result = hotkey_parse_string(argv[3], &b) ? 0 : 1;
            if (result == 0) {
                wchar_t str[64];
                hotkey_to_string(&b, str, 64);
                st_printf(L"parsed: %ls\n", str);
            }
        }
    } else if (wcscmp(sub, L"make-valid-ersm") == 0) {
        if (argc < 4) {
            result = 2;
        } else {
            result = selftest_make_valid_ersm(argv[3]);
        }
    } else if (wcscmp(sub, L"tree-populate") == 0) {
        if (argc < 4) {
            result = 2;
        } else {
            save_tree_t *t = save_tree_create(NULL, NULL, 0);

            if (!t) {
                result = 1;
            } else {
                save_tree_set_root(t, argv[3]);
                st_printf(L"items=%d\n", save_tree_item_count(t));
                save_tree_destroy(t);
                result = 0;
            }
        }
    } else if (wcscmp(sub, L"tree-preserve-selection-walkup") == 0) {
        if (argc < 6) {
            st_printf(L"usage: --selftest tree-preserve-selection-walkup <root> <select_path> <delete_path>\n");
            result = 2;
        } else {
            HWND host = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
                0, 0, 200, 200, NULL, NULL, GetModuleHandleW(NULL), NULL);
            save_tree_t *t;

            if (!host) {
                result = 1;
            } else {
                t = save_tree_create(host, GetModuleHandleW(NULL), 0);
                if (!t) {
                    DestroyWindow(host);
                    result = 1;
                } else {
                    HWND tree_hwnd = save_tree_get_hwnd(t);
                    HTREEITEM select_item;
                    wchar_t delete_full[MAX_PATH];
                    wchar_t expected_relpath[MAX_PATH];
                    wchar_t expected_full[MAX_PATH];
                    wchar_t selected_full[MAX_PATH];

                    save_tree_set_root(t, argv[3]);
                    select_item = selftest_find_tree_item_by_relpath(tree_hwnd, argv[4]);
                    if (!select_item) {
                        st_printf(L"tree-preserve-selection-walkup: selection not found\n");
                        result = 1;
                    } else if (!TreeView_SelectItem(tree_hwnd, select_item)) {
                        st_printf(L"tree-preserve-selection-walkup: selection failed\n");
                        result = 1;
                    } else if (!selftest_build_tree_path(argv[3], argv[5], delete_full, MAX_PATH)) {
                        result = 1;
                    } else if (!selftest_delete_tree_path(delete_full)) {
                        st_printf(L"tree-preserve-selection-walkup: delete failed\n");
                        result = 1;
                    } else {
                        selftest_walk_up_existing_relpath(argv[3], argv[4], expected_relpath, MAX_PATH);
                        if (!selftest_build_tree_path(argv[3], expected_relpath, expected_full, MAX_PATH)) {
                            result = 1;
                        } else {
                            save_tree_refresh_preserve_selection(t);
                            if (!save_tree_get_selected_path(t, selected_full, MAX_PATH)) {
                                st_printf(L"tree-preserve-selection-walkup: no selection after refresh\n");
                                result = 1;
                            } else if (lstrcmpW(selected_full, expected_full) != 0) {
                                st_printf(L"expected=%ls\nselected=%ls\n", expected_full, selected_full);
                                result = 1;
                            } else {
                                st_printf(L"selected=%ls\n", selected_full);
                                result = 0;
                            }
                        }
                    }

                    save_tree_destroy(t);
                    DestroyWindow(host);
                }
            }
        }
    } else if (wcscmp(sub, L"tree-rename") == 0) {
        if (argc < 6) {
            result = 2;
        } else {
            save_tree_t *t = save_tree_create(NULL, NULL, 0);

            if (!t) {
                result = 1;
            } else {
                save_tree_set_root(t, argv[3]);
                result = save_tree_rename(t, argv[4], argv[5]) ? 0 : 1;
                save_tree_destroy(t);
            }
        }
    } else if (wcscmp(sub, L"tree-delete") == 0) {
        if (argc < 5) {
            result = 2;
        } else {
            save_tree_t *t = save_tree_create(NULL, NULL, 0);

            if (!t) {
                result = 1;
            } else {
                save_tree_set_root(t, argv[3]);
                result = save_tree_delete(t, argv[4]) ? 0 : 1;
                save_tree_destroy(t);
            }
        }
    } else if (wcscmp(sub, L"tree-new-folder") == 0) {
        if (argc < 6) {
            result = 2;
        } else {
            save_tree_t *t = save_tree_create(NULL, NULL, 0);

            if (!t) {
                result = 1;
            } else {
                save_tree_set_root(t, argv[3]);
                result = save_tree_new_folder(t, argv[4], argv[5]) ? 0 : 1;
                save_tree_destroy(t);
            }
        }
    } else if (wcscmp(sub, L"tree-move") == 0) {
        if (argc < 6) {
            result = 2;
        } else {
            save_tree_t *t = save_tree_create(NULL, NULL, 0);

            if (!t) {
                result = 1;
            } else {
                save_tree_set_root(t, argv[3]);
                result = save_tree_move(t, argv[4], argv[5]) ? 0 : 1;
                save_tree_destroy(t);
            }
        }
    } else if (wcscmp(sub, L"ring-snapshot") == 0) {
        if (argc < 6) { result = 2; }
        else {
            const game_backend_t *b = backend_registry_get_default();
            ring_backup_init(argv[3], 5);
            wchar_t out[MAX_PATH];
            result = ring_backup_snapshot(b, argv[4], argv[5], 5, out, MAX_PATH) ? 0 : 1;
            if (result == 0) st_printf(L"ring_path=%ls\n", out);
        }
    } else if (wcscmp(sub, L"restore-with-safety") == 0) {
        if (argc < 6) { result = 2; }
        else {
            const game_backend_t *b = backend_registry_get_default();
            ring_backup_init(argv[3], 5);
            result = restore_with_safety(b, argv[4], argv[5], argv[3], 5, false, 0) ? 0 : 1;
        }
    } else if (wcscmp(sub, L"restore-auto-detect") == 0) {
        if (argc < 4) {
            result = 2;
        } else {
            save_kind_t kind = save_compress_classify_backup(argv[3]);
            if (kind == SAVE_KIND_FULL) {
                st_printf(L"FULL\n");
                result = 0;
            } else if (kind == SAVE_KIND_SLOT) {
                st_printf(L"SLOT\n");
                result = 0;
            } else {
                st_printf(L"UNKNOWN\n");
                result = 1;
            }
        }
    } else if (wcscmp(sub, L"undo-last-restore") == 0) {
        if (argc < 4) { result = 2; }
        else {
            const game_backend_t *b = backend_registry_get_default();
            ring_backup_init(argv[3], 5);
            result = undo_last_restore(b, argv[3], 5) ? 0 : 1;
        }
    } else if (wcscmp(sub, L"write-raw-bnd4") == 0) {
        if (argc < 5) {
            st_printf(L"usage: --selftest write-raw-bnd4 <src> <dst>\n");
            result = 2;
        } else {
            HANDLE fh = CreateFileW(argv[3], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (fh == INVALID_HANDLE_VALUE) {
                st_printf(L"write-raw-bnd4: cannot open source\n");
                result = 1;
            } else {
                DWORD fsz = GetFileSize(fh, NULL);
                uint8_t *buf = (uint8_t *)LocalAlloc(LMEM_FIXED, fsz);
                if (!buf) {
                    CloseHandle(fh);
                    result = 1;
                } else {
                    DWORD rd = 0;
                    ReadFile(fh, buf, fsz, &rd, NULL);
                    CloseHandle(fh);
                    bool ok = ersm_write_raw_bnd4_to_file(argv[4], buf, (size_t)rd);
                    LocalFree(buf);
                    if (!ok) {
                        st_printf(L"write-raw-bnd4: write failed (bad magic or I/O)\n");
                        result = 1;
                    } else {
                        st_printf(L"write-raw-bnd4: ok\n");
                        result = 0;
                    }
                }
            }
        }
    } else if (wcscmp(sub, L"classify") == 0) {
        if (argc < 4) {
            st_printf(L"usage: --selftest classify <file>\n");
            result = 2;
        } else {
            save_kind_t kind = save_compress_classify_backup(argv[3]);
            if (kind == SAVE_KIND_FULL)        wprintf(L"FULL\n");
            else if (kind == SAVE_KIND_SLOT)   wprintf(L"SLOT\n");
            else                               wprintf(L"UNKNOWN\n");
            result = 0;
        }
    } else if (wcscmp(sub, L"profile-roundtrip") == 0) {
        if (argc < 4) {
            result = 2;
        } else {
            profile_store_t store;
            profile_store_init(&store);
            /* Build a fixed test store to write and reload. */
            store.games[0].id = 1;
            lstrcpyW(store.games[0].name, L"TestGame");
            store.games[0].game_id = GAME_ID_ELDEN_RING;
            lstrcpyW(store.games[0].tree_root, L"C:\\Test\\Root");
            store.game_count = 1;
            store.backups[0].id = 1;
            store.backups[0].parent_game_id = 1;
            lstrcpyW(store.backups[0].name, L"Main");
            store.backups[0].compression_level = COMP_LEVEL_LOW;
            store.backup_count = 1;
            store.active_game_id = 1;
            store.active_backup_id = 1;
            store.next_game_id = 2;
            store.next_backup_id = 2;

            if (!profile_store_save(&store, argv[3])) {
                st_printf(L"profile-roundtrip: FAIL (save)\n");
                result = 1;
            } else {
                profile_store_t store2;
                profile_store_init(&store2);
                if (!profile_store_load(&store2, argv[3])) {
                    st_printf(L"profile-roundtrip: FAIL (load)\n");
                    result = 1;
                } else {
                    int ok = (int)(store2.game_count == 1)
                           & (int)(store2.backup_count == 1)
                           & (lstrcmpW(store2.games[0].name, L"TestGame") == 0 ? 1 : 0)
                           & (lstrcmpW(store2.backups[0].name, L"Main") == 0 ? 1 : 0)
                           & (store2.active_game_id == 1 ? 1 : 0)
                           & (store2.active_backup_id == 1 ? 1 : 0);
                    result = ok ? 0 : 1;
                    if (result == 0) {
                        st_printf(L"profile-roundtrip: ok\n");
                    } else {
                        st_printf(L"profile-roundtrip: FAIL (compare)\n");
                    }
                }
            }
        }
    } else if (wcscmp(sub, L"profile-load") == 0) {
        if (argc < 4) {
            result = 2;
        } else {
            profile_store_t store;
            profile_store_init(&store);
            profile_store_load(&store, argv[3]);
            st_printf(L"games=%u backups=%u active_game=%d active_backup=%d\n",
                      (unsigned)store.game_count, (unsigned)store.backup_count,
                      store.active_game_id, store.active_backup_id);
            result = 0;
        }
    } else if (wcscmp(sub, L"profile-add-game") == 0) {
        /* --selftest profile-add-game <name> <save_dir> <tree_root> <game_id> <ini> */
        if (argc < 8) {
            result = 2;
        } else {
            profile_store_t store;
            profile_store_init(&store);
            profile_store_load(&store, argv[7]);
            game_profile_t gp;
            ZeroMemory(&gp, sizeof(gp));
            lstrcpynW(gp.name, argv[3], 64);
            lstrcpynW(gp.original_save_dir, argv[4], MAX_PATH);
            lstrcpynW(gp.tree_root, argv[5], MAX_PATH);
            gp.game_id = (game_id_t)_wtoi(argv[6]);
            int new_id = profile_store_add_game(&store, &gp);
            if (new_id == 0) {
                st_printf(L"profile-add-game: FAIL\n");
                result = 1;
            } else {
                profile_store_save(&store, argv[7]);
                st_printf(L"profile-add-game: ok id=%d\n", new_id);
                result = 0;
            }
        }
    } else if (wcscmp(sub, L"profile-add-backup") == 0) {
        /* --selftest profile-add-backup <parent_game_id> <name> <comp_level> <ini> */
        if (argc < 7) {
            result = 2;
        } else {
            profile_store_t store;
            profile_store_init(&store);
            profile_store_load(&store, argv[6]);
            backup_profile_t bp;
            ZeroMemory(&bp, sizeof(bp));
            bp.parent_game_id = _wtoi(argv[3]);
            lstrcpynW(bp.name, argv[4], 64);
            const wchar_t *cl = argv[5];
            if (wcscmp(cl, L"none") == 0) {
                bp.compression_level = COMP_LEVEL_NONE;
            } else if (wcscmp(cl, L"medium") == 0) {
                bp.compression_level = COMP_LEVEL_MEDIUM;
            } else if (wcscmp(cl, L"high") == 0) {
                bp.compression_level = COMP_LEVEL_HIGH;
            } else {
                bp.compression_level = COMP_LEVEL_LOW;
            }
            int new_id = profile_store_add_backup(&store, &bp);
            if (new_id == 0) {
                st_printf(L"profile-add-backup: FAIL\n");
                result = 1;
            } else {
                profile_store_save(&store, argv[6]);
                st_printf(L"profile-add-backup: ok id=%d\n", new_id);
                result = 0;
            }
        }
    } else if (wcscmp(sub, L"profile-list") == 0) {
        /* --selftest profile-list <ini> */
        if (argc < 4) {
            result = 2;
        } else {
            profile_store_t store;
            profile_store_init(&store);
            profile_store_load(&store, argv[3]);
            st_printf(L"games=%zu backups=%zu active_game=%d active_backup=%d\n",
                      store.game_count, store.backup_count,
                      store.active_game_id, store.active_backup_id);
            for (size_t i = 0; i < store.game_count; i++) {
                st_printf(L"  game[%d] name=%ls game_id=%d tree_root=%ls\n",
                          store.games[i].id, store.games[i].name,
                          (int)store.games[i].game_id, store.games[i].tree_root);
            }
            for (size_t i = 0; i < store.backup_count; i++) {
                wchar_t backup_root[MAX_PATH] = {0};
                bool has_root = profile_store_resolve_backup_root(&store, store.backups[i].id,
                    backup_root, MAX_PATH);

                st_printf(L"  backup[%d] parent=%d name=%ls computed_root=%ls comp=%d\n",
                          store.backups[i].id, store.backups[i].parent_game_id,
                          store.backups[i].name, has_root ? backup_root : L"",
                          (int)store.backups[i].compression_level);
            }
            result = 0;
        }
    } else if (wcscmp(sub, L"profile-delete-game") == 0) {
        /* --selftest profile-delete-game <id> <ini> */
        if (argc < 5) {
            result = 2;
        } else {
            profile_store_t store;
            profile_store_init(&store);
            profile_store_load(&store, argv[4]);
            bool ok = profile_store_delete_game(&store, _wtoi(argv[3]));
            if (ok) {
                profile_store_save(&store, argv[4]);
                st_printf(L"profile-delete-game: ok\n");
                result = 0;
            } else {
                st_printf(L"profile-delete-game: not found\n");
                result = 1;
            }
        }
    } else if (wcscmp(sub, L"profile-delete-backup") == 0) {
        /* --selftest profile-delete-backup <id> <ini> */
        if (argc < 5) {
            result = 2;
        } else {
            profile_store_t store;
            profile_store_init(&store);
            profile_store_load(&store, argv[4]);
            bool ok = profile_store_delete_backup(&store, _wtoi(argv[3]));
            if (ok) {
                profile_store_save(&store, argv[4]);
                st_printf(L"profile-delete-backup: ok\n");
                result = 0;
            } else {
                st_printf(L"profile-delete-backup: not found\n");
                result = 1;
            }
        }
    } else if (wcscmp(sub, L"migration-detect") == 0) {
        /* --selftest migration-detect <ini> */
        if (argc < 4) {
            result = 2;
        } else {
            bool needs = profile_store_needs_migration(argv[3]);
            st_printf(needs ? L"true\n" : L"false\n");
            result = 0;
        }
    } else if (wcscmp(sub, L"migration-run") == 0) {
        /* --selftest migration-run <ini> <name> <backup_name> <game_id> <comp_level> <out_ini> */
        if (argc < 9) {
            result = 2;
        } else {
            compression_level_t comp = COMP_LEVEL_LOW;
            if (wcscmp(argv[7], L"none") == 0) {
                comp = COMP_LEVEL_NONE;
            } else if (wcscmp(argv[7], L"high") == 0) {
                comp = COMP_LEVEL_HIGH;
            }
            profile_store_t store;
            profile_store_init(&store);
            bool ok = profile_store_migrate(&store, argv[3], argv[4], argv[5],
                                            (game_id_t)_wtoi(argv[6]), comp, argv[8]);
            if (!ok) {
                st_printf(L"migration-run: FAIL\n");
                result = 1;
            } else {
                st_printf(L"migration-run: ok games=%zu backups=%zu\n",
                          store.game_count, store.backup_count);
                result = 0;
            }
        }
    } else if (wcscmp(sub, L"locale-dump") == 0) {
        /* --selftest locale-dump: print all STR_PRAXIS_* locale strings */
        for (int i = 0; i < (int)STR_PRAXIS_MAX; i++) {
            st_printf(L"%d: %ls\n", i, praxis_locale_str((praxis_string_index_t)i));
        }
        result = 0;
    } else if (wcscmp(sub, L"watcher-state") == 0) {
        /* --selftest watcher-state <tree_root>: validate watcher can start and stop */
        if (argc < 4) {
            result = 2;
        } else {
            save_watcher_t *watcher = save_watcher_start(NULL, argv[3], WM_APP + 1);
            if (!watcher) {
                st_printf(L"watcher-state: failed to start\n");
                result = 1;
            } else {
                Sleep(500);  /* brief wait */
                save_watcher_stop(watcher);
                st_printf(L"watcher-state: ok\n");
                result = 0;
            }
        }
    } else if (wcscmp(sub, L"backup-full-with-active") == 0) {
        /* --selftest backup-full-with-active <src_sl2> <dst_backup>: backup full with active backend */
        if (argc < 5) {
            result = 2;
        } else {
            const game_backend_t *b = backend_registry_get_default();
            if (!b || !b->backup_full) {
                st_printf(L"backup-full-with-active: no backend\n");
                result = 1;
            } else {
                bool ok = b->backup_full(argv[3], argv[4], 5);
                if (!ok) {
                    st_printf(L"backup-full-with-active: backup failed\n");
                    result = 1;
                } else {
                    st_printf(L"backup-full-with-active: ok\n");
                    result = 0;
                }
            }
        }
    } else {
        /* Placeholder subcommands added in T18, T20-T22, T23, T25, T26, T29. */
        st_printf(L"unknown selftest subcommand: %ls\n", sub);
        result = 2;
    }

    LocalFree(argv);
    return result;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, LPWSTR cmd_line, int cmd_show) {
    (void)prev_instance;
    HRESULT com_hr;
    bool com_initialized;

    /* Initialize common controls (TreeView + ToolBar/StatusBar family + ListView + Standard). */
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    /* Enable visual styles for native-looking controls. */
    SetThemeAppProperties(STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS | STAP_ALLOW_WEBCONTENT);

    com_hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    com_initialized = SUCCEEDED(com_hr) || com_hr == S_FALSE;

    /* Initialize LZMA SDK (idempotent). */
    save_compress_init();

    /* Parse --log-file flag up front so selftest runs can also log if needed. */
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc - 1; i++) {
            if (wcscmp(argv[i], L"--log-file") == 0) {
                g_log_file = CreateFileW(argv[i + 1], GENERIC_WRITE, FILE_SHARE_READ,
                    NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                break;
            }
        }
        LocalFree(argv);
    }

    /* --selftest: headless QA mode — runs tests and exits without showing a window. */
    if (cmd_line && cmd_line[0] != L'\0' && wcsncmp(cmd_line, L"--selftest", 10) == 0) {
        int selftest_result = run_selftest();
        if (com_initialized) CoUninitialize();
        return selftest_result;
    }

    /* Load configuration and apply language preference. We deliberately do NOT
     * call praxis_save_config() here: it writes only the [Settings] section
     * and would clobber any [GameProfile:N]/[BackupProfile:N] sections that
     * profile_store_save() persisted in a previous session. The INI is
     * written via profile_store_save() (which preserves all sections) on
     * every profile mutation and on WM_CLOSE. */
    praxis_load_config();
    praxis_locale_set_current(praxis_config.language);

    /* First-launch setup: when there are no game profiles AND user hasn't
     * dismissed migration, show the Add Game Profile dialog pre-filled with
     * legacy values (if a legacy Praxis.ini exists). Skipped if user clicked
     * Cancel previously (MigrationDismissed=1). */
    {
        wchar_t ini_path[MAX_PATH];
        if (config_core_get_app_ini_path(ini_path, MAX_PATH, L"Praxis.ini") &&
            !praxis_config.migration_dismissed) {
            /* Detect: profile store empty? */
            profile_store_t probe_store;
            profile_store_init(&probe_store);
            profile_store_load(&probe_store, ini_path);
            bool needs_first_launch_setup = (probe_store.game_count == 0);

            if (needs_first_launch_setup) {
                /* Pre-fill from legacy [Settings].TreeRoot if present. */
                game_profile_t pre_gp;
                ZeroMemory(&pre_gp, sizeof(pre_gp));
                pre_gp.game_id = GAME_ID_ELDEN_RING;
                lstrcpynW(pre_gp.name, L"Default", 64);
                if (praxis_config.tree_root[0] != L'\0') {
                    lstrcpynW(pre_gp.tree_root, praxis_config.tree_root, MAX_PATH);
                }

                INT_PTR result = edit_game_profile(NULL, &pre_gp, true);
                if (result == IDOK) {
                    /* User confirmed -- add the profile (auto-creates Main backup). */
                    profile_store_add_game(&probe_store, &pre_gp);
                    profile_store_save(&probe_store, ini_path);
                } else {
                    /* User cancelled: set dismissed flag so we don't re-prompt.
                     * Use profile_store_save (not praxis_save_config) so the
                     * full INI -- including any pre-existing profile sections
                     * loaded into probe_store -- is preserved. */
                    praxis_config.migration_dismissed = 1;
                    profile_store_save(&probe_store, ini_path);
                }
            }
        }
    }

    /* Register window class. */
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = praxis_wnd_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MAIN_MENU);
    wc.lpszClassName = L"PRAXIS_MAIN";
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) {
        if (com_initialized) CoUninitialize();
        return 1;
    }

    /* Resolve window geometry from config (-1 / 0 → system defaults). */
    int x = praxis_config.window_x == -1 ? CW_USEDEFAULT : praxis_config.window_x;
    int y = praxis_config.window_y == -1 ? CW_USEDEFAULT : praxis_config.window_y;
    int w = praxis_config.window_width == 0 ? 800 : praxis_config.window_width;
    int h = praxis_config.window_height == 0 ? 600 : praxis_config.window_height;

    HWND hwnd = CreateWindowExW(0, L"PRAXIS_MAIN",
        praxis_locale_str(STR_PRAXIS_APP_TITLE),
        WS_OVERLAPPEDWINDOW,
        x, y, w, h,
        NULL, NULL, instance, NULL);
    if (!hwnd) {
        if (com_initialized) CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, cmd_show);
    UpdateWindow(hwnd);

    /* Standard message pump. */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (com_initialized) CoUninitialize();
    return (int)msg.wParam;
}
