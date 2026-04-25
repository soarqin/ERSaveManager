/**
 * @file hotkey.h
 * @brief RegisterHotKey wrapper with MOD_NOREPEAT and conflict handling.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <windows.h>

typedef enum hotkey_id_e {
    HOTKEY_BACKUP_FULL = 1,
    HOTKEY_BACKUP_SLOT = 2,
    HOTKEY_RESTORE = 3,  /* unified, auto-detects full vs slot */
    HOTKEY_UNDO_RESTORE = 4
} hotkey_id_t;

typedef struct hotkey_binding_s {
    UINT modifiers; /* MOD_CONTROL, MOD_SHIFT, etc. (NOT including MOD_NOREPEAT) */
    UINT vk;        /* Virtual key code */
} hotkey_binding_t;

bool hotkey_init(HWND target_window);
bool hotkey_register(hotkey_id_t id, const hotkey_binding_t *binding);
bool hotkey_unregister(hotkey_id_t id);
void hotkey_unregister_all(void);
bool hotkey_parse_string(const wchar_t *s, hotkey_binding_t *out);
bool hotkey_to_string(const hotkey_binding_t *b, wchar_t *out, size_t out_chars);
