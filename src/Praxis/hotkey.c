/**
 * @file hotkey.c
 * @brief Hotkey registration with MOD_NOREPEAT and conflict detection.
 */

#include "hotkey.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

#include <windows.h>

static HWND g_target_window = NULL;
static hotkey_binding_t g_bindings[6]; /* index 0 unused; 1-5 = hotkey_id_t */
static bool g_registered[6];

bool hotkey_init(HWND target_window) {
    g_target_window = target_window;
    ZeroMemory(g_bindings, sizeof(g_bindings));
    ZeroMemory(g_registered, sizeof(g_registered));
    return true;
}

bool hotkey_register(hotkey_id_t id, const hotkey_binding_t *binding) {
    UINT mods;

    if (!g_target_window || id < HOTKEY_BACKUP_FULL || id > HOTKEY_UNDO_RESTORE || !binding || binding->vk == 0) {
        return false;
    }

    if (g_registered[id]) {
        hotkey_unregister(id);
    }

    mods = binding->modifiers | MOD_NOREPEAT;
    if (!RegisterHotKey(g_target_window, (int)id, mods, binding->vk)) {
        if (GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED) {
            return false;
        }
        return false;
    }

    g_bindings[id] = *binding;
    g_registered[id] = true;
    return true;
}

bool hotkey_unregister(hotkey_id_t id) {
    if (!g_target_window || id < HOTKEY_BACKUP_FULL || id > HOTKEY_UNDO_RESTORE) {
        return false;
    }

    if (!g_registered[id]) {
        return true;
    }

    UnregisterHotKey(g_target_window, (int)id);
    ZeroMemory(&g_bindings[id], sizeof(g_bindings[id]));
    g_registered[id] = false;
    return true;
}

void hotkey_unregister_all(void) {
    for (int i = HOTKEY_BACKUP_FULL; i <= HOTKEY_UNDO_RESTORE; i++) {
        hotkey_unregister((hotkey_id_t)i);
    }
}

bool hotkey_parse_string(const wchar_t *s, hotkey_binding_t *out) {
    wchar_t buf[64];
    wchar_t *ctx = NULL;
    wchar_t *tok;
    UINT mods = 0;
    UINT vk = 0;

    if (!s || !out || s[0] == L'\0') {
        return false;
    }

    ZeroMemory(out, sizeof(*out));
    lstrcpynW(buf, s, (int)(sizeof(buf) / sizeof(buf[0])));

    tok = wcstok(buf, L"+", &ctx);
    while (tok) {
        if (lstrcmpiW(tok, L"Ctrl") == 0) {
            mods |= MOD_CONTROL;
        } else if (lstrcmpiW(tok, L"Shift") == 0) {
            mods |= MOD_SHIFT;
        } else if (lstrcmpiW(tok, L"Alt") == 0) {
            mods |= MOD_ALT;
        } else if (lstrcmpiW(tok, L"Win") == 0) {
            mods |= MOD_WIN;
        } else if (tok[0] == L'F' && tok[1] >= L'1' && tok[1] <= L'9') {
            int fn = _wtoi(tok + 1);

            if (fn >= 1 && fn <= 12) {
                vk = VK_F1 + (UINT)fn - 1;
            } else {
                return false;
            }
        } else if (lstrlenW(tok) == 1) {
            SHORT scan = VkKeyScanW(towupper(tok[0]));

            if (scan == -1) {
                return false;
            }
            vk = (UINT)(scan & 0xFF);
        } else {
            return false;
        }

        tok = wcstok(NULL, L"+", &ctx);
    }

    if (vk == 0) {
        return false;
    }

    out->modifiers = mods;
    out->vk = vk;
    return true;
}

bool hotkey_to_string(const hotkey_binding_t *b, wchar_t *out, size_t out_chars) {
    wchar_t buf[64] = {0};

    if (!b || !out || out_chars == 0) {
        return false;
    }

    if (b->modifiers & MOD_CONTROL) {
        lstrcatW(buf, L"Ctrl+");
    }
    if (b->modifiers & MOD_SHIFT) {
        lstrcatW(buf, L"Shift+");
    }
    if (b->modifiers & MOD_ALT) {
        lstrcatW(buf, L"Alt+");
    }
    if (b->modifiers & MOD_WIN) {
        lstrcatW(buf, L"Win+");
    }

    if (b->vk >= VK_F1 && b->vk <= VK_F12) {
        wchar_t fn[8];

        _snwprintf(fn, sizeof(fn) / sizeof(fn[0]), L"F%u", (unsigned)(b->vk - VK_F1 + 1));
        fn[(sizeof(fn) / sizeof(fn[0])) - 1] = L'\0';
        lstrcatW(buf, fn);
    } else if (b->vk >= 'A' && b->vk <= 'Z') {
        wchar_t ch[2] = { (wchar_t)b->vk, 0 };
        lstrcatW(buf, ch);
    } else {
        lstrcatW(buf, L"?");
    }

    if ((size_t)lstrlenW(buf) >= out_chars) {
        return false;
    }

    lstrcpyW(out, buf);
    return true;
}
