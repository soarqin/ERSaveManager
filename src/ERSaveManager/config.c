/**
 * @file config.c
 * @brief Configuration management implementation
 * @details This file contains the implementation of configuration management
 *          functions for the Elden Ring face data manager.
 *          Uses direct WinAPI file I/O with a single-pass INI parser
 *          for efficient batch read/write operations.
 */

#include "config.h"

#include "locale.h"

#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>

/* INI file configuration constants */
#define CONFIG_FILE L"ERSaveManager.ini"          /* Configuration file name */
#define CONFIG_SECTION L"Settings"                /* Main section name */
#define CONFIG_SAVE_PATH L"SavePath"              /* Save path key */
#define CONFIG_SAVE_SUBFOLDER L"SaveSubFolder"    /* Save subfolder key */
#define CONFIG_LANGUAGE L"Language"               /* Language setting key */
#define CONFIG_WINDOW_X L"WindowX"                /* Window X position key */
#define CONFIG_WINDOW_Y L"WindowY"                /* Window Y position key */
#define CONFIG_WINDOW_WIDTH L"WindowWidth"        /* Window width key */
#define CONFIG_WINDOW_HEIGHT L"WindowHeight"      /* Window height key */
#define CONFIG_COMPRESSION_LEVEL L"CompressionLevel" /* Compression level key */

#define INI_WRITE_BUF_SIZE 4096                   /* Write buffer size in bytes */

/* Global configuration variable */
config_t config = {0};

/* Main window handle - defined in main.c */
extern HWND main_window;

/* ==== INI write helpers ==== */

/**
 * @brief Buffer writer for building INI file content in memory
 * @details Accumulates UTF-8 bytes into a fixed buffer, then the
 *          caller flushes to disk in a single WriteFile call.
 */
typedef struct ini_buf_s {
    char data[INI_WRITE_BUF_SIZE];
    int len;
} ini_buf_t;

/* Append a raw ASCII/UTF-8 null-terminated string to the buffer */
static void buf_append(ini_buf_t* buf, const char* str) {
    while (*str && buf->len < INI_WRITE_BUF_SIZE - 1) {
        buf->data[buf->len++] = *str++;
    }
}

/* Append a wide string converted to UTF-8 */
static void buf_append_wide(ini_buf_t* buf, const wchar_t* wstr) {
    int avail = INI_WRITE_BUF_SIZE - buf->len;
    if (avail <= 1) return;
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1,
                                  buf->data + buf->len, avail, NULL, NULL);
    if (len > 0) buf->len += len - 1; /* exclude null terminator */
}

/* Append a decimal integer */
static void buf_append_int(ini_buf_t* buf, int value) {
    char tmp[16];
    wsprintfA(tmp, "%d", value);
    buf_append(buf, tmp);
}

/* Write a key=value entry with a wide string value */
static void buf_write_str(ini_buf_t* buf, const wchar_t* key, const wchar_t* value) {
    buf_append_wide(buf, key);
    buf_append(buf, "=");
    buf_append_wide(buf, value);
    buf_append(buf, "\r\n");
}

/* Write a key=value entry with an integer value */
static void buf_write_int(ini_buf_t* buf, const wchar_t* key, int value) {
    buf_append_wide(buf, key);
    buf_append(buf, "=");
    buf_append_int(buf, value);
    buf_append(buf, "\r\n");
}

/* Build and write the entire INI file in a single I/O operation */
static void write_ini_file(const wchar_t* filename) {
    ini_buf_t buf = {0};

    /* Section header */
    buf_append(&buf, "[");
    buf_append_wide(&buf, CONFIG_SECTION);
    buf_append(&buf, "]\r\n");

    /* String entries */
    buf_write_str(&buf, CONFIG_SAVE_PATH, config.save_path);
    buf_write_str(&buf, CONFIG_SAVE_SUBFOLDER, config.save_subfolder);

    /* Integer entries */
    buf_write_int(&buf, CONFIG_LANGUAGE, config.language);
    buf_write_int(&buf, CONFIG_WINDOW_X, config.window_x);
    buf_write_int(&buf, CONFIG_WINDOW_Y, config.window_y);
    buf_write_int(&buf, CONFIG_WINDOW_WIDTH, config.window_width);
    buf_write_int(&buf, CONFIG_WINDOW_HEIGHT, config.window_height);
    buf_write_int(&buf, CONFIG_COMPRESSION_LEVEL, config.compression_level);

    /* Flush entire buffer to file at once */
    HANDLE hFile = CreateFileW(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, buf.data, (DWORD)buf.len, &written, NULL);
        CloseHandle(hFile);
    }
}

/* ==== INI read helpers ==== */

/**
 * @brief Check if a UTF-8 byte span matches a wide string constant
 * @details Only valid for ASCII-only key/section names, which all
 *          configuration constants in this file are.
 * @param key      Pointer to the UTF-8 byte span (not null-terminated)
 * @param key_len  Length of the byte span
 * @param wkey     Null-terminated wide string to compare against
 * @return true if the strings are equal
 */
static bool key_matches(const char* key, int key_len, const wchar_t* wkey) {
    for (int i = 0; i < key_len; i++) {
        if ((wchar_t)(unsigned char)key[i] != wkey[i]) return false;
    }
    return wkey[key_len] == L'\0';
}

/**
 * @brief Parse a decimal integer from a non-null-terminated byte span
 * @param str  Pointer to the digit characters
 * @param len  Number of bytes to consider
 * @return Parsed integer value
 */
static int parse_int(const char* str, int len) {
    int result = 0;
    int sign = 1;
    int i = 0;

    if (i < len && str[i] == '-') {
        sign = -1;
        i++;
    }
    while (i < len && str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }
    return sign * result;
}

/* Convert a UTF-8 byte span to a wide string with null terminator */
static void store_wide_value(const char* val, int val_len, wchar_t* dest, int dest_size) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, val, val_len, dest, dest_size - 1);
    dest[wlen] = L'\0';
}

/**
 * @brief Single-pass INI parser
 * @details Reads all key-value pairs from the [Settings] section in one
 *          traversal of the buffer. Handles UTF-8 BOM, comments (';', '#'),
 *          and whitespace trimming around keys and values.
 * @param data  Raw file content (UTF-8)
 * @param size  Number of bytes in data
 * @param cfg   Configuration structure to populate
 */
static void parse_ini_buffer(const char* data, DWORD size, config_t* cfg) {
    const char* p = data;
    const char* end = data + size;
    bool in_settings = false;

    /* Skip UTF-8 BOM if present */
    if (size >= 3 && (unsigned char)p[0] == 0xEF &&
        (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        p += 3;
    }

    while (p < end) {
        /* Skip leading whitespace */
        while (p < end && (*p == ' ' || *p == '\t')) p++;

        /* Find end of line */
        const char* line_start = p;
        while (p < end && *p != '\r' && *p != '\n') p++;
        const char* line_end = p;

        /* Advance past line endings */
        while (p < end && (*p == '\r' || *p == '\n')) p++;

        int line_len = (int)(line_end - line_start);
        if (line_len == 0) continue;

        /* Skip comment lines */
        if (line_start[0] == ';' || line_start[0] == '#') continue;

        /* Section header: [Name] */
        if (line_start[0] == '[') {
            const char* close = line_start + 1;
            while (close < line_end && *close != ']') close++;
            if (close < line_end) {
                int sec_len = (int)(close - line_start - 1);
                in_settings = key_matches(line_start + 1, sec_len, CONFIG_SECTION);
            }
            continue;
        }

        if (!in_settings) continue;

        /* Find '=' separator */
        const char* eq = line_start;
        while (eq < line_end && *eq != '=') eq++;
        if (eq >= line_end) continue;

        /* Extract key with trailing whitespace trimmed */
        const char* key = line_start;
        int key_len = (int)(eq - line_start);
        while (key_len > 0 && (key[key_len - 1] == ' ' || key[key_len - 1] == '\t')) key_len--;

        /* Extract value with surrounding whitespace trimmed */
        const char* val = eq + 1;
        while (val < line_end && (*val == ' ' || *val == '\t')) val++;
        int val_len = (int)(line_end - val);
        while (val_len > 0 && (val[val_len - 1] == ' ' || val[val_len - 1] == '\t')) val_len--;

        /* Dispatch on known keys */
        if (key_matches(key, key_len, CONFIG_SAVE_PATH)) {
            store_wide_value(val, val_len, cfg->save_path, MAX_PATH);
        } else if (key_matches(key, key_len, CONFIG_SAVE_SUBFOLDER)) {
            store_wide_value(val, val_len, cfg->save_subfolder, 32);
        } else if (key_matches(key, key_len, CONFIG_LANGUAGE)) {
            cfg->language = parse_int(val, val_len);
        } else if (key_matches(key, key_len, CONFIG_WINDOW_X)) {
            cfg->window_x = parse_int(val, val_len);
        } else if (key_matches(key, key_len, CONFIG_WINDOW_Y)) {
            cfg->window_y = parse_int(val, val_len);
        } else if (key_matches(key, key_len, CONFIG_WINDOW_WIDTH)) {
            cfg->window_width = parse_int(val, val_len);
        } else if (key_matches(key, key_len, CONFIG_WINDOW_HEIGHT)) {
            cfg->window_height = parse_int(val, val_len);
        } else if (key_matches(key, key_len, CONFIG_COMPRESSION_LEVEL)) {
            cfg->compression_level = parse_int(val, val_len);
        }
    }
}

/* ==== Public API ==== */

/**
 * @brief Load configuration from INI file
 * @details Reads the entire file into memory with a single ReadFile call,
 *          then parses all settings in one pass. Falls back to defaults
 *          when the file does not exist or cannot be read.
 */
void load_config(void) {
    /* Get path to configuration file */
    wchar_t config_path[MAX_PATH];
    GetModuleFileNameW(NULL, config_path, MAX_PATH);
    PathRemoveFileSpecW(config_path);
    PathAppendW(config_path, CONFIG_FILE);

    /* Initialize configuration structure with default values */
    ZeroMemory(&config, sizeof(config_t));
    config.language = detect_system_language();
    config.window_x = -1;
    config.window_y = -1;
    config.window_width = 0;
    config.window_height = 0;
    config.compression_level = 5;

    /* Read entire file into memory and parse in one pass */
    HANDLE hFile = CreateFileW(config_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD file_size = GetFileSize(hFile, NULL);
        if (file_size != INVALID_FILE_SIZE && file_size > 0) {
            char* buf = (char*)LocalAlloc(LMEM_FIXED, file_size);
            if (buf) {
                DWORD bytes_read;
                if (ReadFile(hFile, buf, file_size, &bytes_read, NULL)) {
                    parse_ini_buffer(buf, bytes_read, &config);
                }
                LocalFree(buf);
            }
        }
        CloseHandle(hFile);
    }

    /* Clamp compression_level to valid range */
    if (config.compression_level < 0 || config.compression_level > 9) {
        config.compression_level = 5;
    }

    /* If save path is empty, use default AppData path */
    if (config.save_path[0] == L'\0') {
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, config.save_path))) {
            PathAppendW(config.save_path, L"\\EldenRing");
        }
    }

    /* Set current locale based on configuration */
    set_current_locale(config.language);
}

/**
 * @brief Save configuration to INI file
 * @details Builds the entire INI content in memory, then writes it
 *          to disk in a single WriteFile call.
 */
void save_config(void) {
    wchar_t config_path[MAX_PATH];
    GetModuleFileNameW(NULL, config_path, MAX_PATH);
    PathRemoveFileSpecW(config_path);
    PathAppendW(config_path, CONFIG_FILE);

    config.language = get_current_locale();

    /* If main window exists, get its position and size */
    if (main_window) {
        RECT rc;
        GetWindowRect(main_window, &rc);
        config.window_x = rc.left;
        config.window_y = rc.top;
        config.window_width = rc.right - rc.left;
        config.window_height = rc.bottom - rc.top;
    }

    /* Write INI file */
    write_ini_file(config_path);
}
