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
#include "praxis_hotkey_actions.h"
#include "save_tree.h"
#include "save_watcher.h"
#include "profile_store.h"
#include "profile_store_io.h"
#include "praxis_main_menu.h"
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
#include <wchar.h>

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <uxtheme.h>

/** @brief Global main window handle (set on WM_CREATE). */
typedef struct praxis_app_s {
    HWND main_window;
    save_tree_t *save_tree;
    save_watcher_t *save_watcher;
    HWND status_bar;
    toolbar_t *toolbar;
} praxis_app_t;

static praxis_app_t g_app = {0};
static profile_store_t g_profile_store;

/** @brief Log file handle opened via --log-file flag (for Gate I testing). */
static HANDLE g_log_file = INVALID_HANDLE_VALUE;

/* Forward declarations */
static LRESULT CALLBACK praxis_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

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

    return profile_store_io_save(&g_profile_store, ini);
}

static void set_active_status_text(void) {
    const game_profile_t *gp = NULL;
    const backup_profile_t *bp;
    wchar_t status[256];

    if (!g_app.status_bar) {
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
        SetWindowTextW(g_app.status_bar, status);
        return;
    }

    SetWindowTextW(g_app.status_bar, praxis_locale_str(STR_PRAXIS_APP_TITLE));
}

static void apply_active_profile_ui(HWND hwnd) {
    const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);
    wchar_t backup_root[MAX_PATH];

    if (g_app.toolbar) {
        toolbar_set_selected_backup_id(g_app.toolbar, bp ? bp->id : 0);
        toolbar_set_actions_enabled(g_app.toolbar, bp != NULL);
    }

    if (bp == NULL ||
        !profile_store_resolve_backup_root(&g_profile_store, bp->id, backup_root, MAX_PATH)) {
        set_active_status_text();
        return;
    }

    if (g_app.save_tree) {
        save_tree_set_root(g_app.save_tree, backup_root);
    }

    if (g_app.save_watcher) {
        save_watcher_change_root(g_app.save_watcher, backup_root);
    } else {
        g_app.save_watcher = save_watcher_start(hwnd, backup_root, WM_WATCHER_NOTIFY);
    }

    set_active_status_text();
}

static LRESULT CALLBACK praxis_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        {
            CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
            hotkey_binding_t b;

            g_app.save_tree = save_tree_create(hwnd, cs->hInstance, IDC_TREE_VIEW);
            if (!g_app.save_tree) {
                return -1;
            }

            g_app.toolbar = toolbar_create(hwnd, cs->hInstance);
            if (g_app.toolbar) {
                RECT client_rect;

                if (GetClientRect(hwnd, &client_rect)) {
                    int cw = client_rect.right - client_rect.left;
                    int top_h = toolbar_get_top_height(g_app.toolbar);
                    /* Initial layout — WM_SIZE will refine y_top once the
                     * status bar is also created and measured. */
                    toolbar_layout_top(g_app.toolbar, cw);
                    toolbar_layout_bottom(g_app.toolbar, cw, top_h);
                }
            }

            g_app.status_bar = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                0, 0, 0, 0, hwnd, (HMENU)(uintptr_t)IDC_STATUS_BAR, cs->hInstance, NULL);
            if (!g_app.status_bar) {
                if (g_app.toolbar) {
                    toolbar_destroy(g_app.toolbar);
                    g_app.toolbar = NULL;
                }
                save_tree_destroy(g_app.save_tree);
                g_app.save_tree = NULL;
                return -1;
            }

            save_tree_set_root(g_app.save_tree, praxis_config.tree_root);

            if (g_app.save_tree && praxis_config.tree_root[0] != L'\0') {
                g_app.save_watcher = save_watcher_start(hwnd, praxis_config.tree_root, WM_WATCHER_NOTIFY);
            }

            /* Load profile store and populate toolbar combobox. */
            profile_store_init(&g_profile_store);
            {
                wchar_t ini[MAX_PATH];
                if (config_core_get_app_ini_path(ini, MAX_PATH, L"Praxis.ini")) {
                    profile_store_io_load(&g_profile_store, ini);
                }
                if (g_app.toolbar) toolbar_populate_profiles(g_app.toolbar, &g_profile_store);
            }

            apply_active_profile_ui(hwnd);

            g_app.main_window = hwnd;
            praxis_main_menu_apply_locale_strings(hwnd);

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

        int top_h    = g_app.toolbar ? toolbar_get_top_height(g_app.toolbar)    : 0;
        int bottom_h = g_app.toolbar ? toolbar_get_bottom_height(g_app.toolbar) : 0;

        /* Top toolbar at y=0 */
        if (g_app.toolbar) {
            toolbar_layout_top(g_app.toolbar, client_width);
        }

        /* Status bar at very bottom (auto-positions itself) */
        int status_height = 0;
        if (g_app.status_bar) {
            SendMessageW(g_app.status_bar, WM_SIZE, wp, lp);  /* let status bar resize itself */
            RECT sr;
            GetWindowRect(g_app.status_bar, &sr);
            status_height = sr.bottom - sr.top;
        }

        /* Bottom toolbar above status bar */
        int bottom_y = client_height - status_height - bottom_h;
        if (bottom_y < top_h) bottom_y = top_h;
        if (g_app.toolbar) {
            toolbar_layout_bottom(g_app.toolbar, client_width, bottom_y);
        }

        /* TreeView fills middle: from top_h down to bottom_y */
        if (g_app.save_tree) {
            HWND htree = save_tree_get_hwnd(g_app.save_tree);
            if (htree) {
                int tree_h = bottom_y - top_h;
                if (tree_h < 0) tree_h = 0;
                MoveWindow(htree, 0, top_h, client_width, tree_h, TRUE);
            }
        }
        return 0;
    }

    case WM_NOTIFY:
        if (g_app.save_tree) {
            LRESULT notify_result = 0;

            if (save_tree_handle_notify(g_app.save_tree, (LPNMHDR)lp, &notify_result)) {
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
            if (g_app.save_tree) {
                save_tree_refresh_preserve_selection(g_app.save_tree);
            }
        }
        return 0;

    case WM_COMMAND:
        /* Dynamic Game/Language submenu commands — delegate to menu module. */
        if (praxis_main_menu_handle_command(hwnd, wp, &g_profile_store)) {
            /* Refresh toolbar and active profile UI after any dynamic menu command.
             * populate_profiles and apply_locale_strings cover both the game-profile
             * and language-change cases; over-calling either is harmless. */
            if (g_app.toolbar) {
                toolbar_populate_profiles(g_app.toolbar, &g_profile_store);
                toolbar_apply_locale_strings(g_app.toolbar);
            }
            apply_active_profile_ui(hwnd);
            return 0;
        }
        if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == IDC_PROFILE_COMBO) {
            int selected_id = toolbar_get_selected_backup_id(g_app.toolbar);

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
                if (g_app.toolbar) toolbar_populate_profiles(g_app.toolbar, &g_profile_store);
                apply_active_profile_ui(hwnd);
            }
            return 0;
        case IDC_BTN_BACKUP_FULL:
            {
                const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);
                praxis_hotkey_action_backup_full(hwnd, &g_profile_store, g_app.save_tree,
                    bp ? (int)bp->compression_level : (int)COMP_LEVEL_NONE);
            }
            return 0;
        case IDC_BTN_BACKUP_SLOT:
            {
                const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);
                praxis_hotkey_action_backup_slot(hwnd, &g_profile_store, g_app.save_tree,
                    bp ? (int)bp->compression_level : (int)COMP_LEVEL_NONE);
            }
            return 0;
        case IDC_BTN_RESTORE:
            praxis_hotkey_action_restore(hwnd, &g_profile_store, g_app.save_tree);
            return 0;
        case IDC_BTN_UNDO:
            if (praxis_hotkey_action_undo(hwnd, &g_profile_store) && g_app.save_tree) {
                save_tree_refresh(g_app.save_tree);
            }
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
                    if (g_app.toolbar) {
                        toolbar_populate_profiles(g_app.toolbar, &g_profile_store);
                    }
                    apply_active_profile_ui(hwnd);
                }
            }
            return 0;
        case IDC_BTN_DEL_BACKUP:
            {
                int backup_id = toolbar_get_selected_backup_id(g_app.toolbar);
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
                    if (g_app.toolbar) {
                        toolbar_populate_profiles(g_app.toolbar, &g_profile_store);
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

    case WM_INITMENUPOPUP:
        praxis_main_menu_init_popup(hwnd, (HMENU)wp, &g_profile_store);
        return 0;

    case WM_HOTKEY: {
            wchar_t log_msg[64];
            const game_backend_t *backend = get_active_backend();

            _snwprintf(log_msg, 64, L"HOTKEY_FIRED id=%d\n", (int)wp);
            log_msg[63] = L'\0';
            log_write(log_msg);

            if (!backend) return 0;

            switch ((hotkey_id_t)wp) {
            case HOTKEY_BACKUP_FULL:
                {
                    const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);
                    praxis_hotkey_action_backup_full(hwnd, &g_profile_store, g_app.save_tree,
                        bp ? (int)bp->compression_level : (int)COMP_LEVEL_NONE);
                }
                break;
            case HOTKEY_BACKUP_SLOT:
                {
                    const backup_profile_t *bp = profile_store_get_active_backup(&g_profile_store);
                    praxis_hotkey_action_backup_slot(hwnd, &g_profile_store, g_app.save_tree,
                        bp ? (int)bp->compression_level : (int)COMP_LEVEL_NONE);
                }
                break;
            case HOTKEY_RESTORE:
                praxis_hotkey_action_restore(hwnd, &g_profile_store, g_app.save_tree);
                break;
            case HOTKEY_UNDO_RESTORE:
                if (praxis_hotkey_action_undo(hwnd, &g_profile_store) && g_app.save_tree) {
                    save_tree_refresh(g_app.save_tree);
                }
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
        if (g_app.save_watcher) {
            save_watcher_stop(g_app.save_watcher);
            g_app.save_watcher = NULL;
        }
        KillTimer(hwnd, IDT_REFRESH_DEBOUNCE);
        if (g_app.toolbar) {
            toolbar_destroy(g_app.toolbar);
            g_app.toolbar = NULL;
        }
        save_tree_destroy(g_app.save_tree);
        g_app.save_tree = NULL;
        g_app.status_bar = NULL;
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
     * profile_store_io_save() persisted in a previous session. The INI is
     * written via profile_store_io_save() (which preserves all sections) on
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
            profile_store_io_load(&probe_store, ini_path);
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
                    profile_store_io_save(&probe_store, ini_path);
                } else {
                    /* User cancelled: set dismissed flag so we don't re-prompt.
                     * Use profile_store_io_save (not praxis_save_config) so the
                     * full INI -- including any pre-existing profile sections
                     * loaded into probe_store -- is preserved. */
                    praxis_config.migration_dismissed = 1;
                    profile_store_io_save(&probe_store, ini_path);
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
