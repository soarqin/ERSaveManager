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
#include "praxis_selftest.h"
#include "toolbar.h"
#include "dialogs/edit_game_profile.h"
#include "dialogs/game_profile_manager.h"
#include "dialogs/edit_backup_profile.h"
#include "dialogs/hotkey_settings.h"

#include "ersave.h"
#include "save_compress.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
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
static void apply_main_menu_locale_strings(HWND hwnd);

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

static const game_backend_t *get_active_backend(void) {
    const game_profile_t *gp = NULL;
    const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);

    if (bp) {
        gp = profile_store_find_game_by_id(&g_profile_store, bp->parent_game_id);
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

/* Exposed (non-static) so dialogs/hotkey_settings.c can persist the INI
 * after committing new bindings without reaching into private state. */
bool save_profile_store(void) {
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
        gp = profile_store_find_game_by_id(&g_profile_store, bp->parent_game_id);
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
        gp = profile_store_find_game_by_id(&g_profile_store, bp->parent_game_id);
    }
    if (!gp) {
        gp = profile_store_get_active_game(&g_profile_store);
    }
    if (gp && bp) {
        _snwprintf_s(status, 256, _TRUNCATE, praxis_locale_str(STR_PRAXIS_STATUS_ACTIVE), gp->name, bp->name);
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

static void apply_main_menu_locale_strings(HWND hwnd) {
    HMENU menu;
    HMENU file_menu;
    HMENU game_menu;
    HMENU options_menu;
    HMENU language_menu;

    if (!hwnd) {
        return;
    }

    menu = GetMenu(hwnd);
    if (!menu) {
        return;
    }

    file_menu = GetSubMenu(menu, 0);
    game_menu = GetSubMenu(menu, 1);
    options_menu = GetSubMenu(menu, 2);

    if (file_menu) {
        ModifyMenuW(menu, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)file_menu,
            praxis_locale_str(STR_PRAXIS_FILE));
        ModifyMenuW(file_menu, IDM_FILE_EXIT, MF_BYCOMMAND | MF_STRING, IDM_FILE_EXIT,
            praxis_locale_str(STR_PRAXIS_EXIT));
    }

    if (game_menu) {
        ModifyMenuW(menu, 1, MF_BYPOSITION | MF_POPUP, (UINT_PTR)game_menu,
            praxis_locale_str(STR_PRAXIS_GAME));
        ModifyMenuW(game_menu, IDM_GAME_MANAGE, MF_BYCOMMAND | MF_STRING, IDM_GAME_MANAGE,
            praxis_locale_str(STR_PRAXIS_MANAGE_GAME_PROFILES));
    }

    if (options_menu) {
        ModifyMenuW(menu, 2, MF_BYPOSITION | MF_POPUP, (UINT_PTR)options_menu,
            praxis_locale_str(STR_PRAXIS_OPTIONS));
        ModifyMenuW(options_menu, IDM_OPTIONS_HOTKEYS, MF_BYCOMMAND | MF_STRING, IDM_OPTIONS_HOTKEYS,
            praxis_locale_str(STR_PRAXIS_HOTKEY_SETTINGS));

        language_menu = GetSubMenu(options_menu, 1);
        if (language_menu) {
            ModifyMenuW(options_menu, 1, MF_BYPOSITION | MF_POPUP, (UINT_PTR)language_menu,
                praxis_locale_str(STR_PRAXIS_LANGUAGE));
        }
    }

    DrawMenuBar(hwnd);
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

    ok = restore_safe_auto(backend, selected_path, save_path, backup_root,
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

    ok = restore_safe_undo(backend, backup_root, comp_level_to_lzma(bp->compression_level));
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
            apply_main_menu_locale_strings(hwnd);

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
        /* Dynamic Language submenu selection */
        if (LOWORD(wp) >= IDM_LANG_FIRST && LOWORD(wp) <= IDM_LANG_LAST) {
            int idx = (int)(LOWORD(wp) - IDM_LANG_FIRST);
            if (idx >= 0 && idx < praxis_locale_count() && idx != praxis_locale_get_current()) {
                praxis_locale_set_current(idx);
                praxis_config.language = idx;
                save_profile_store();
                /* Refresh visible UI strings without requiring a restart.
                 * The Language submenu itself is rebuilt on next
                 * WM_INITMENUPOPUP so its checkmark moves automatically. */
                if (g_toolbar) {
                    toolbar_apply_locale_strings(g_toolbar);
                }
                SetWindowTextW(hwnd, praxis_locale_str(STR_PRAXIS_APP_TITLE));
                apply_main_menu_locale_strings(hwnd);
                set_active_status_text();
            }
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
                dialog_game_profile_manager_show(hwnd, &g_profile_store, ini);
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

                if (dialog_edit_backup_profile_show(hwnd, &new_bp, true) != IDOK) {
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
            dialog_hotkey_settings_show(hwnd);
            return 0;
        }
        return 0;

    case WM_INITMENUPOPUP: {
            HMENU sub = (HMENU)wp;
            int count = GetMenuItemCount(sub);
            bool is_game_menu = false;
            bool is_lang_menu = false;

            /* Identify the popup by inspecting its current item IDs:
             *   - Game submenu always contains IDM_GAME_MANAGE.
             *   - Language submenu contains IDM_OPTIONS_LANG (the static
             *     "English" placeholder from the .rc) on first open, or any
             *     id in [IDM_LANG_FIRST, IDM_LANG_LAST] after rebuild. */
            for (int i = 0; i < count; i++) {
                UINT id = GetMenuItemID(sub, i);
                if (id == IDM_GAME_MANAGE) {
                    is_game_menu = true;
                    break;
                }
                if (id == IDM_OPTIONS_LANG ||
                    (id >= IDM_LANG_FIRST && id <= IDM_LANG_LAST)) {
                    is_lang_menu = true;
                    break;
                }
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
                return 0;
            }

            if (is_lang_menu) {
                /* Wipe the entire submenu (sentinel placeholder + any prior
                 * dynamic items) and rebuild it from the locale catalog with
                 * the active locale checked. */
                int n;
                int cur;

                while (GetMenuItemCount(sub) > 0) {
                    DeleteMenu(sub, 0, MF_BYPOSITION);
                }

                n = praxis_locale_count();
                cur = praxis_locale_get_current();
                for (int i = 0; i < n; i++) {
                    UINT flags = MF_BYPOSITION | MF_STRING;
                    if (i == cur) {
                        flags |= MF_CHECKED;
                    }
                    InsertMenuW(sub, i, flags,
                                (UINT_PTR)(IDM_LANG_FIRST + i),
                                praxis_locale_name(i));
                }
                return 0;
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

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, LPWSTR cmd_line, int cmd_show) {
    (void)prev_instance;
    (void)cmd_line;
    HRESULT com_hr;
    bool com_initialized;

    /* Initialize common controls (TreeView + ToolBar/StatusBar family + ListView + Standard + Hotkey). */
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES
               | ICC_STANDARD_CLASSES | ICC_HOTKEY_CLASS;
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
    }

    /* --selftest: headless QA mode — runs tests and exits without showing a window. */
    if (argv && argc > 1 && wcscmp(argv[1], L"--selftest") == 0) {
        int selftest_result = praxis_selftest_run(argc, argv);
        LocalFree(argv);
        if (com_initialized) CoUninitialize();
        return selftest_result;
    }

    if (argv) {
        LocalFree(argv);
    }

    /* Load configuration and apply language preference. We deliberately do NOT
     * call praxis_save_config() here: it writes only the [Settings] section
     * and would clobber any [GameProfile:N]/[BackupProfile:N] sections that
     * profile_store_save() persisted in a previous session. The INI is
     * written via profile_store_save() (which preserves all sections) on
     * every profile mutation and on WM_CLOSE. */
    praxis_load_config();
    praxis_locale_set_current(praxis_config.language);

    /* First-launch setup: when there are no game profiles and the prompt has
     * not been dismissed, show the Add Game Profile dialog pre-filled from the
     * current config values. Skipped if the user cancelled previously
     * (MigrationDismissed=1). */
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
                /* Pre-fill from the current TreeRoot value if present. */
                game_profile_t pre_gp;
                ZeroMemory(&pre_gp, sizeof(pre_gp));
                pre_gp.game_id = GAME_ID_ELDEN_RING;
                lstrcpynW(pre_gp.name, L"Default", 64);
                if (praxis_config.tree_root[0] != L'\0') {
                    lstrcpynW(pre_gp.tree_root, praxis_config.tree_root, MAX_PATH);
                }

                INT_PTR result = dialog_edit_game_profile_show(NULL, &pre_gp, true);
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
