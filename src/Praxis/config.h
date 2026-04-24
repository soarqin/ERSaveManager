/**
 * @file config.h
 * @brief Praxis application configuration.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

/* Praxis-specific config struct (NOT config_t — avoids name collision) */
typedef struct praxis_config_s {
    wchar_t tree_root[MAX_PATH];    /* Root directory for save library */
    int language;                   /* Locale index (0 = English) */
    int window_x;                   /* Window position X (-1 = unset) */
    int window_y;                   /* Window position Y (-1 = unset) */
    int window_width;               /* Window width (0 = default) */
    int window_height;              /* Window height (0 = default) */
    int compression_level;          /* LZMA compression level (1-9, default 5) */
    int ring_size;                  /* Ring backup slot count (default 5) */
    wchar_t hotkey_backup_full[32]; /* e.g. "Ctrl+Shift+F5" */
    wchar_t hotkey_restore_full[32];
    wchar_t hotkey_backup_slot[32];
    wchar_t hotkey_restore_slot[32];
    wchar_t hotkey_undo_restore[32];
} praxis_config_t;

extern praxis_config_t praxis_config;

void praxis_load_config(void);
void praxis_save_config(void);
