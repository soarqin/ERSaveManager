/**
 * @file main.c
 * @brief Praxis application entry point and main window procedure.
 */

#include "config.h"
#include "locale.h"
#include "resource.h"

#include "save_compress.h"

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
        g_main_window = hwnd;
        return 0;

    case WM_SIZE:
        /* Layout placeholder — TreeView added in T19 */
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_FILE_EXIT:
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        return 0;

    case WM_HOTKEY:
        /* Placeholder — full dispatch added in T27 */
        {
            wchar_t log_msg[64];
            _snwprintf(log_msg, 64, L"HOTKEY_FIRED id=%d\n", (int)wp);
            log_write(log_msg);
        }
        return 0;

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
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_log_file != INVALID_HANDLE_VALUE) {
            CloseHandle(g_log_file);
            g_log_file = INVALID_HANDLE_VALUE;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
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

    /* Initialize common controls (TreeView + ToolBar/StatusBar family). */
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    /* Enable visual styles for native-looking controls. */
    SetThemeAppProperties(STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS | STAP_ALLOW_WEBCONTENT);

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
        return run_selftest();
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

    return (int)msg.wParam;
}
