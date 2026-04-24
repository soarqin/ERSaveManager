/**
 * @file config.c
 * @brief Praxis configuration load/save using config_core toolkit.
 */

#include "config.h"
#include "locale.h"

#include "config_core.h"
#include "locale_core.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <string.h>
#include <windows.h>

praxis_config_t praxis_config;

static void apply_defaults(void) {
    wchar_t docs[MAX_PATH] = {0};

    ZeroMemory(&praxis_config, sizeof(praxis_config));

    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, docs))) {
        lstrcpyW(praxis_config.tree_root, docs);
        PathAppendW(praxis_config.tree_root, L"Praxis");
    }

    praxis_config.language = 0;
    praxis_config.window_x = -1;
    praxis_config.window_y = -1;
    praxis_config.window_width = 0;
    praxis_config.window_height = 0;
    praxis_config.compression_level = 5;
    praxis_config.ring_size = 5;
    lstrcpyW(praxis_config.hotkey_backup_full, L"Ctrl+Shift+F5");
    lstrcpyW(praxis_config.hotkey_restore_full, L"Ctrl+Shift+F9");
    lstrcpyW(praxis_config.hotkey_backup_slot, L"Ctrl+Shift+F6");
    lstrcpyW(praxis_config.hotkey_restore_slot, L"Ctrl+Shift+F10");
    lstrcpyW(praxis_config.hotkey_undo_restore, L"Ctrl+Shift+Z");
}

static void kv_callback(const char *key, const char *value, void *user) {
    praxis_config_t *cfg = (praxis_config_t *)user;

    if (strcmp(key, "TreeRoot") == 0) {
        config_core_store_wide_value(cfg->tree_root, MAX_PATH, value);
    } else if (strcmp(key, "Language") == 0) {
        cfg->language = config_core_parse_int(value, 0);
    } else if (strcmp(key, "WindowX") == 0) {
        cfg->window_x = config_core_parse_int(value, -1);
    } else if (strcmp(key, "WindowY") == 0) {
        cfg->window_y = config_core_parse_int(value, -1);
    } else if (strcmp(key, "WindowWidth") == 0) {
        cfg->window_width = config_core_parse_int(value, 0);
    } else if (strcmp(key, "WindowHeight") == 0) {
        cfg->window_height = config_core_parse_int(value, 0);
    } else if (strcmp(key, "CompressionLevel") == 0) {
        cfg->compression_level = config_core_parse_int(value, 5);
    } else if (strcmp(key, "RingSize") == 0) {
        cfg->ring_size = config_core_parse_int(value, 5);
    } else if (strcmp(key, "HotkeyBackupFull") == 0) {
        config_core_store_wide_value(cfg->hotkey_backup_full, 32, value);
    } else if (strcmp(key, "HotkeyRestoreFull") == 0) {
        config_core_store_wide_value(cfg->hotkey_restore_full, 32, value);
    } else if (strcmp(key, "HotkeyBackupSlot") == 0) {
        config_core_store_wide_value(cfg->hotkey_backup_slot, 32, value);
    } else if (strcmp(key, "HotkeyRestoreSlot") == 0) {
        config_core_store_wide_value(cfg->hotkey_restore_slot, 32, value);
    } else if (strcmp(key, "HotkeyUndoRestore") == 0) {
        config_core_store_wide_value(cfg->hotkey_undo_restore, 32, value);
    }
}

void praxis_load_config(void) {
    apply_defaults();

    wchar_t ini_path[MAX_PATH];
    if (!config_core_get_app_ini_path(ini_path, MAX_PATH, L"Praxis.ini")) {
        return;
    }

    HANDLE fh = CreateFileW(ini_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD file_size = GetFileSize(fh, NULL);
    if (file_size == INVALID_FILE_SIZE || file_size == 0) {
        CloseHandle(fh);
        return;
    }

    char *buf = LocalAlloc(LMEM_FIXED, file_size + 1);
    if (!buf) {
        CloseHandle(fh);
        return;
    }

    DWORD bytes_read;
    if (!ReadFile(fh, buf, file_size, &bytes_read, NULL)) {
        LocalFree(buf);
        CloseHandle(fh);
        return;
    }
    buf[bytes_read] = '\0';
    CloseHandle(fh);

    config_core_parse_ini(buf, bytes_read, kv_callback, &praxis_config);
    LocalFree(buf);

    locale_core_set_current(praxis_config.language);
}

void praxis_save_config(void) {
    wchar_t ini_path[MAX_PATH];
    if (!config_core_get_app_ini_path(ini_path, MAX_PATH, L"Praxis.ini")) {
        return;
    }

    char tree_root_utf8[MAX_PATH * 4];
    char hotkey_bf[64], hotkey_rf[64], hotkey_bs[64], hotkey_rs[64], hotkey_ur[64];

    WideCharToMultiByte(CP_UTF8, 0, praxis_config.tree_root, -1, tree_root_utf8, sizeof(tree_root_utf8), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, praxis_config.hotkey_backup_full, -1, hotkey_bf, sizeof(hotkey_bf), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, praxis_config.hotkey_restore_full, -1, hotkey_rf, sizeof(hotkey_rf), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, praxis_config.hotkey_backup_slot, -1, hotkey_bs, sizeof(hotkey_bs), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, praxis_config.hotkey_restore_slot, -1, hotkey_rs, sizeof(hotkey_rs), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, praxis_config.hotkey_undo_restore, -1, hotkey_ur, sizeof(hotkey_ur), NULL, NULL);

    config_core_buf_t buf;
    config_core_buf_init(&buf);
    config_core_buf_append(&buf, "[Settings]\r\n");
    config_core_buf_append(&buf, "TreeRoot=%s\r\n", tree_root_utf8);
    config_core_buf_append(&buf, "Language=%d\r\n", praxis_config.language);
    config_core_buf_append(&buf, "WindowX=%d\r\n", praxis_config.window_x);
    config_core_buf_append(&buf, "WindowY=%d\r\n", praxis_config.window_y);
    config_core_buf_append(&buf, "WindowWidth=%d\r\n", praxis_config.window_width);
    config_core_buf_append(&buf, "WindowHeight=%d\r\n", praxis_config.window_height);
    config_core_buf_append(&buf, "CompressionLevel=%d\r\n", praxis_config.compression_level);
    config_core_buf_append(&buf, "RingSize=%d\r\n", praxis_config.ring_size);
    config_core_buf_append(&buf, "HotkeyBackupFull=%s\r\n", hotkey_bf);
    config_core_buf_append(&buf, "HotkeyRestoreFull=%s\r\n", hotkey_rf);
    config_core_buf_append(&buf, "HotkeyBackupSlot=%s\r\n", hotkey_bs);
    config_core_buf_append(&buf, "HotkeyRestoreSlot=%s\r\n", hotkey_rs);
    config_core_buf_append(&buf, "HotkeyUndoRestore=%s\r\n", hotkey_ur);
    config_core_buf_write_file(&buf, ini_path);
    config_core_buf_free(&buf);
}
