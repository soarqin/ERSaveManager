/**
 * @file main.c
 * @brief Praxis application entry point and main window procedure.
 */

#include "config.h"
#include "backend_registry.h"
#include "hotkey.h"
#include "locale.h"
#include "resource.h"
#include "file_dialog.h"
#include "ring_backup.h"
#include "restore_safe.h"
#include "save_tree.h"

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
#include <shlwapi.h>
#include <uxtheme.h>

/** @brief Global main window handle (set on WM_CREATE). */
static HWND g_main_window = NULL;
static save_tree_t *g_save_tree = NULL;
static HWND g_status_bar = NULL;

/** @brief Log file handle opened via --log-file flag (for Gate I testing). */
static HANDLE g_log_file = INVALID_HANDLE_VALUE;

/* Forward declarations */
static LRESULT CALLBACK praxis_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static int run_selftest(void);

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

            g_status_bar = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                0, 0, 0, 0, hwnd, (HMENU)(uintptr_t)IDC_STATUS_BAR, cs->hInstance, NULL);
            if (!g_status_bar) {
                save_tree_destroy(g_save_tree);
                g_save_tree = NULL;
                return -1;
            }

            save_tree_set_root(g_save_tree, praxis_config.tree_root);

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

            SendMessageW(g_status_bar, SB_SETTEXTW, 0, (LPARAM)praxis_locale_str(STR_PRAXIS_APP_TITLE));
        }
        return 0;

    case WM_SIZE:
        if (g_status_bar) {
            SendMessageW(g_status_bar, WM_SIZE, wp, lp);
        }
        if (g_save_tree && save_tree_get_hwnd(g_save_tree)) {
            RECT client_rect;
            RECT status_rect;
            int status_height = 0;

            GetClientRect(hwnd, &client_rect);
            if (g_status_bar && GetWindowRect(g_status_bar, &status_rect)) {
                status_height = status_rect.bottom - status_rect.top;
            }
            MoveWindow(save_tree_get_hwnd(g_save_tree),
                0,
                0,
                client_rect.right - client_rect.left,
                (client_rect.bottom - client_rect.top) - status_height,
                TRUE);
        }
        return 0;

    case WM_NOTIFY:
        if (g_save_tree) {
            LRESULT notify_result = 0;

            if (save_tree_handle_notify(g_save_tree, (LPNMHDR)lp, &notify_result)) {
                return notify_result;
            }
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_BACKUP_FULL:
            {
                const game_backend_t *b = backend_registry_get_default();

                if (b) {
                    wchar_t save_path[MAX_PATH];

                    if (b->resolve_save_path(save_path, MAX_PATH)) {
                        SYSTEMTIME st;
                        wchar_t fname[MAX_PATH];

                        GetSystemTime(&st);
                        _snwprintf(fname, MAX_PATH, L"%ls\\manual_%04d%02d%02d%02d%02d%02d.ersm",
                            praxis_config.tree_root, st.wYear, st.wMonth, st.wDay,
                            st.wHour, st.wMinute, st.wSecond);
                        fname[MAX_PATH - 1] = L'\0';
                        b->backup_full(save_path, fname, praxis_config.compression_level);
                        if (g_save_tree) save_tree_refresh(g_save_tree);
                    }
                }
            }
            return 0;
        case IDM_BACKUP_SLOT:
        case IDM_RESTORE_SLOT:
            return 0;
        case IDM_RESTORE_SEL:
            {
                const game_backend_t *b = backend_registry_get_default();

                if (b && g_save_tree) {
                    wchar_t sel[MAX_PATH], save_path[MAX_PATH];

                    if (save_tree_get_selected_path(g_save_tree, sel, MAX_PATH) &&
                        b->resolve_save_path(save_path, MAX_PATH)) {
                        ring_backup_init(praxis_config.tree_root, praxis_config.ring_size);
                        restore_with_safety(b, sel, save_path, praxis_config.tree_root,
                            praxis_config.compression_level, false, 0);
                    }
                }
            }
            return 0;
        case IDM_RESTORE_UNDO:
            {
                const game_backend_t *b = backend_registry_get_default();

                if (b) {
                    ring_backup_init(praxis_config.tree_root, praxis_config.ring_size);
                    undo_last_restore(b, praxis_config.tree_root, praxis_config.compression_level);
                }
            }
            return 0;
        case IDM_FILE_SET_ROOT:
            {
                wchar_t *new_root = file_dialog_open_folder(hwnd, praxis_config.tree_root);

                if (new_root) {
                    lstrcpynW(praxis_config.tree_root, new_root, MAX_PATH);
                    CoTaskMemFree(new_root);
                    if (g_save_tree) save_tree_set_root(g_save_tree, praxis_config.tree_root);
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

    case WM_HOTKEY: {
            wchar_t log_msg[64];
            const game_backend_t *backend = backend_registry_get_default();
            wchar_t save_path[MAX_PATH];

            _snwprintf(log_msg, 64, L"HOTKEY_FIRED id=%d\n", (int)wp);
            log_msg[63] = L'\0';
            log_write(log_msg);

            if (!backend) return 0;
            if (!backend->resolve_save_path(save_path, MAX_PATH)) {
                return 0;
            }

            switch ((hotkey_id_t)wp) {
            case HOTKEY_BACKUP_FULL:
                {
                    SYSTEMTIME st;
                    wchar_t fname[MAX_PATH];

                    GetSystemTime(&st);
                    _snwprintf(fname, MAX_PATH, L"%ls\\manual_%04d%02d%02d%02d%02d%02d.ersm",
                        praxis_config.tree_root, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                    fname[MAX_PATH - 1] = L'\0';
                    backend->backup_full(save_path, fname, praxis_config.compression_level);
                    if (g_save_tree) save_tree_refresh(g_save_tree);
                    break;
                }
            case HOTKEY_BACKUP_SLOT:
                if (game_backend_supports_slot_ops(backend)) {
                    int slot = 0;

                    if (backend->get_active_slot(save_path, &slot)) {
                        SYSTEMTIME st2;
                        wchar_t fname2[MAX_PATH];

                        GetSystemTime(&st2);
                        _snwprintf(fname2, MAX_PATH, L"%ls\\slot%d_%04d%02d%02d%02d%02d%02d.ersm",
                            praxis_config.tree_root, slot, st2.wYear, st2.wMonth, st2.wDay,
                            st2.wHour, st2.wMinute, st2.wSecond);
                        fname2[MAX_PATH - 1] = L'\0';
                        backend->backup_slot(save_path, slot, fname2, praxis_config.compression_level);
                        if (g_save_tree) save_tree_refresh(g_save_tree);
                    }
                }
                break;
            case HOTKEY_RESTORE:
                {
                    /* TODO(T19): wire to active profile and call restore_with_safety_auto */
                    /* Placeholder: keep build green until T10/T19 complete */
                    wchar_t sel[MAX_PATH];

                    if (g_save_tree && save_tree_get_selected_path(g_save_tree, sel, MAX_PATH)) {
                        ring_backup_init(praxis_config.tree_root, praxis_config.ring_size);
                        restore_with_safety(backend, sel, save_path, praxis_config.tree_root,
                            praxis_config.compression_level, false, 0);
                    }
                    break;
                }
            case HOTKEY_UNDO_RESTORE:
                ring_backup_init(praxis_config.tree_root, praxis_config.ring_size);
                undo_last_restore(backend, praxis_config.tree_root, praxis_config.compression_level);
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
        praxis_save_config();
        hotkey_unregister_all();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
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

    /* Load configuration and apply language preference.
     * Immediately persist so a default Praxis.ini materializes on first launch
     * even if the user closes the app via a hard-kill that bypasses WM_DESTROY. */
    praxis_load_config();
    praxis_locale_set_current(praxis_config.language);
    praxis_save_config();

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
