/**
 * @file config.c
 * @brief Configuration management implementation
 * @details Glues ERSaveManager's config_t onto the generic config_core
 *          utility toolkit. Path building, INI parsing, integer conversion,
 *          and output buffering live in src/common/config_core; this file
 *          owns the struct, the field list, and the load/save orchestration.
 */

#include "config.h"

#include "locale.h"

#include "config_core.h"

#include <string.h>

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

/* INI file name resolved against the executable directory */
#define CONFIG_FILE L"ERSaveManager.ini"

/* INI key names (ASCII only - matches the wide names historically used) */
#define CONFIG_KEY_SAVE_PATH         "SavePath"
#define CONFIG_KEY_SAVE_SUBFOLDER    "SaveSubFolder"
#define CONFIG_KEY_LANGUAGE          "Language"
#define CONFIG_KEY_WINDOW_X          "WindowX"
#define CONFIG_KEY_WINDOW_Y          "WindowY"
#define CONFIG_KEY_WINDOW_WIDTH      "WindowWidth"
#define CONFIG_KEY_WINDOW_HEIGHT     "WindowHeight"
#define CONFIG_KEY_COMPRESSION_LEVEL "CompressionLevel"
#define CONFIG_KEY_THEME             "Theme"

/* Upper bound for each UTF-8 path before conversion. Worst case is 4 bytes per
 * wchar_t for UTF-8, so size the intermediate buffer accordingly. */
#define CONFIG_UTF8_PATH_BYTES (MAX_PATH * 4)
#define CONFIG_UTF8_SUBFOLDER_BYTES (32 * 4)

/* Global configuration variable (zero-initialised at program start) */
config_t config;

/* Main window handle - defined in main.c */
extern HWND main_window;

/* ==== INI read ==== */

/**
 * @brief Callback invoked by config_core_parse_ini for each [Settings] entry.
 */
static void on_settings_kv(const char *key, const char *value, void *user) {
    config_t *cfg = (config_t *)user;

    if (strcmp(key, CONFIG_KEY_SAVE_PATH) == 0) {
        config_core_store_wide_value(cfg->save_path, MAX_PATH, value);
    } else if (strcmp(key, CONFIG_KEY_SAVE_SUBFOLDER) == 0) {
        config_core_store_wide_value(cfg->save_subfolder, 32, value);
    } else if (strcmp(key, CONFIG_KEY_LANGUAGE) == 0) {
        cfg->language = config_core_parse_int(value, cfg->language);
    } else if (strcmp(key, CONFIG_KEY_WINDOW_X) == 0) {
        cfg->window_x = config_core_parse_int(value, cfg->window_x);
    } else if (strcmp(key, CONFIG_KEY_WINDOW_Y) == 0) {
        cfg->window_y = config_core_parse_int(value, cfg->window_y);
    } else if (strcmp(key, CONFIG_KEY_WINDOW_WIDTH) == 0) {
        cfg->window_width = config_core_parse_int(value, cfg->window_width);
    } else if (strcmp(key, CONFIG_KEY_WINDOW_HEIGHT) == 0) {
        cfg->window_height = config_core_parse_int(value, cfg->window_height);
    } else if (strcmp(key, CONFIG_KEY_COMPRESSION_LEVEL) == 0) {
        cfg->compression_level = config_core_parse_int(value, cfg->compression_level);
    } else if (strcmp(key, CONFIG_KEY_THEME) == 0) {
        cfg->theme = config_core_parse_int(value, cfg->theme);
    }
}

/* ==== INI write ==== */

/**
 * @brief Appends key=wide_value as UTF-8 to the output buffer.
 */
static void append_wide_kv(config_core_buf_t *buf, const char *key, const wchar_t *wvalue,
                           char *utf8_scratch, int scratch_bytes) {
    int converted = WideCharToMultiByte(CP_UTF8, 0, wvalue, -1,
                                        utf8_scratch, scratch_bytes, NULL, NULL);
    if (converted <= 0) {
        utf8_scratch[0] = '\0';
    }
    config_core_buf_append(buf, "%s=%s\r\n", key, utf8_scratch);
}

/**
 * @brief Builds the entire INI content in memory and flushes it to path in one write.
 */
static void write_ini_file(const wchar_t *path) {
    config_core_buf_t buf;
    config_core_buf_init(&buf);

    config_core_buf_append(&buf, "[Settings]\r\n");

    char path_utf8[CONFIG_UTF8_PATH_BYTES];
    char subfolder_utf8[CONFIG_UTF8_SUBFOLDER_BYTES];
    append_wide_kv(&buf, CONFIG_KEY_SAVE_PATH, config.save_path,
                   path_utf8, (int)sizeof(path_utf8));
    append_wide_kv(&buf, CONFIG_KEY_SAVE_SUBFOLDER, config.save_subfolder,
                   subfolder_utf8, (int)sizeof(subfolder_utf8));

    config_core_buf_append(&buf, "%s=%d\r\n", CONFIG_KEY_LANGUAGE,          config.language);
    config_core_buf_append(&buf, "%s=%d\r\n", CONFIG_KEY_WINDOW_X,          config.window_x);
    config_core_buf_append(&buf, "%s=%d\r\n", CONFIG_KEY_WINDOW_Y,          config.window_y);
    config_core_buf_append(&buf, "%s=%d\r\n", CONFIG_KEY_WINDOW_WIDTH,      config.window_width);
    config_core_buf_append(&buf, "%s=%d\r\n", CONFIG_KEY_WINDOW_HEIGHT,     config.window_height);
    config_core_buf_append(&buf, "%s=%d\r\n", CONFIG_KEY_COMPRESSION_LEVEL, config.compression_level);
    config_core_buf_append(&buf, "%s=%d\r\n", CONFIG_KEY_THEME,             config.theme);

    config_core_buf_write_file(&buf, path);
    config_core_buf_free(&buf);
}

/* ==== Public API ==== */

/**
 * @brief Load configuration from INI file
 * @details Reads the entire file into memory with a single ReadFile call,
 *          then parses all settings in one pass via config_core_parse_ini.
 *          Falls back to defaults when the file does not exist or cannot be read.
 */
void load_config(void) {
    /* Resolve <exe-dir>\ERSaveManager.ini */
    wchar_t config_path[MAX_PATH];
    if (!config_core_get_app_ini_path(config_path, MAX_PATH, CONFIG_FILE)) {
        /* Path resolution failed: fall back to defaults without touching disk. */
        config_path[0] = L'\0';
    }

    /* Initialise the configuration structure with default values. */
    ZeroMemory(&config, sizeof(config_t));
    config.language = detect_system_language();
    config.window_x = -1;
    config.window_y = -1;
    config.window_width = 0;
    config.window_height = 0;
    config.compression_level = 5;

    /* Read the entire file into memory and parse it in one pass. */
    if (config_path[0] != L'\0') {
        HANDLE hFile = CreateFileW(config_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD file_size = GetFileSize(hFile, NULL);
            if (file_size != INVALID_FILE_SIZE && file_size > 0) {
                char *buf = (char *)LocalAlloc(LMEM_FIXED, file_size);
                if (buf) {
                    DWORD bytes_read = 0;
                    if (ReadFile(hFile, buf, file_size, &bytes_read, NULL)) {
                        config_core_parse_ini(buf, bytes_read, on_settings_kv, &config);
                    }
                    LocalFree(buf);
                }
            }
            CloseHandle(hFile);
        }
    }

    /* Clamp compression_level to the valid 0..9 range. */
    if (config.compression_level < 0 || config.compression_level > 9) {
        config.compression_level = 5;
    }

    /* Clamp theme to the valid 0..2 range (System/Light/Dark). */
    if (config.theme < 0 || config.theme > 2) {
        config.theme = 0;
    }

    /* If save path is empty, use the default AppData path. */
    if (config.save_path[0] == L'\0') {
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, config.save_path))) {
            PathAppendW(config.save_path, L"\\EldenRing");
        }
    }

    /* Set current locale based on configuration. */
    set_current_locale(config.language);
}

/**
 * @brief Save configuration to INI file
 * @details Builds the entire INI content in memory with config_core_buf_t, then
 *          writes it to disk in a single WriteFile call.
 */
void save_config(void) {
    wchar_t config_path[MAX_PATH];
    if (!config_core_get_app_ini_path(config_path, MAX_PATH, CONFIG_FILE)) {
        /* Without a path we cannot persist settings; skip silently. */
        return;
    }

    config.language = get_current_locale();

    /* If the main window exists, capture its current position and size. */
    if (main_window) {
        RECT rc;
        GetWindowRect(main_window, &rc);
        config.window_x = rc.left;
        config.window_y = rc.top;
        config.window_width = rc.right - rc.left;
        config.window_height = rc.bottom - rc.top;
    }

    write_ini_file(config_path);
}
