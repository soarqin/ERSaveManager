/**
 * @file config_core.c
 * @brief Generic INI config utility toolkit implementation.
 * @details Provides reusable primitives for ERSaveManager-family applications:
 *          executable-directory path resolution, integer parsing, UTF-8 to
 *          wide conversion, a callback-driven [Settings] parser, and a
 *          growable output buffer backed by LocalAlloc/LocalFree.
 *          No application state lives here; callers own their own config_t.
 */

#include "config_core.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <shlwapi.h>

/* Section name recognised by config_core_parse_ini */
#define CONFIG_CORE_SECTION "Settings"

/* Initial capacity for config_core_buf_t on its first append */
#define CONFIG_CORE_BUF_INITIAL_CAPACITY ((size_t)256)

/* Maximum key length accepted by the parser (including null terminator) */
#define CONFIG_CORE_MAX_KEY 256

/* Maximum value length accepted by the parser (including null terminator) */
#define CONFIG_CORE_MAX_VALUE 1024

/* ==== Path resolution ==== */

bool config_core_get_app_ini_path(wchar_t *out, size_t out_chars, const wchar_t *app_ini_name) {
    if (out == NULL || out_chars == 0 || app_ini_name == NULL) {
        return false;
    }

    /* GetModuleFileNameW returns the number of characters written (excluding the
     * null terminator). If the buffer is too small it returns out_chars and the
     * path is truncated; treat that as failure. */
    DWORD written = GetModuleFileNameW(NULL, out, (DWORD)out_chars);
    if (written == 0 || written >= out_chars) {
        if (out_chars > 0) {
            out[0] = L'\0';
        }
        return false;
    }

    /* Strip the executable file name, leaving the containing directory. */
    if (!PathRemoveFileSpecW(out)) {
        return false;
    }

    /* Append the application-supplied INI file name (e.g. L"ERSaveManager.ini"). */
    if (!PathAppendW(out, app_ini_name)) {
        return false;
    }

    return true;
}

/* ==== Integer parsing ==== */

int config_core_parse_int(const char *value, int defval) {
    if (value == NULL) {
        return defval;
    }

    /* Skip leading whitespace so callers do not need to trim ahead of time. */
    const char *p = value;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        return defval;
    }

    char *endp = NULL;
    errno = 0;
    long parsed = strtol(p, &endp, 10);
    if (endp == p) {
        /* Nothing consumed: not a number. */
        return defval;
    }
    if (errno == ERANGE) {
        return defval;
    }
    if (parsed < INT_MIN) {
        return INT_MIN;
    }
    if (parsed > INT_MAX) {
        return INT_MAX;
    }
    return (int)parsed;
}

/* ==== UTF-8 to wide ==== */

bool config_core_store_wide_value(wchar_t *out, size_t out_chars, const char *utf8_value) {
    if (out == NULL || out_chars == 0) {
        return false;
    }

    if (utf8_value == NULL) {
        out[0] = L'\0';
        return true;
    }

    /* Using -1 for the source length asks Win32 to copy the null terminator
     * into the destination. On success the return value includes that null. */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_value, -1, out, (int)out_chars);
    if (wlen == 0) {
        out[0] = L'\0';
        return false;
    }
    return true;
}

/* ==== INI parser ==== */

/**
 * @brief Compare a byte span against a null-terminated ASCII string.
 * @return true if the spans match exactly (length and content).
 */
static bool span_equals_ascii(const char *span, size_t span_len, const char *ascii) {
    size_t ascii_len = strlen(ascii);
    if (span_len != ascii_len) {
        return false;
    }
    return memcmp(span, ascii, span_len) == 0;
}

bool config_core_parse_ini(const char *buffer, size_t length, config_core_kv_callback cb, void *user) {
    if (buffer == NULL || cb == NULL) {
        return false;
    }

    const char *p = buffer;
    const char *end = buffer + length;
    bool in_settings = false;

    /* Skip UTF-8 BOM if present so the first line parses as expected. */
    if (length >= 3
        && (unsigned char)p[0] == 0xEF
        && (unsigned char)p[1] == 0xBB
        && (unsigned char)p[2] == 0xBF) {
        p += 3;
    }

    while (p < end) {
        /* Skip leading whitespace so indented lines are still recognised. */
        while (p < end && (*p == ' ' || *p == '\t')) {
            p++;
        }

        /* Locate the end of the current line (before the line terminator). */
        const char *line_start = p;
        while (p < end && *p != '\r' && *p != '\n') {
            p++;
        }
        const char *line_end = p;

        /* Consume line-terminator bytes so the next iteration starts cleanly. */
        while (p < end && (*p == '\r' || *p == '\n')) {
            p++;
        }

        size_t line_len = (size_t)(line_end - line_start);
        if (line_len == 0) {
            continue;
        }

        /* Comments: lines starting with ';' or '#'. */
        if (line_start[0] == ';' || line_start[0] == '#') {
            continue;
        }

        /* Section header: [Name] */
        if (line_start[0] == '[') {
            const char *close = line_start + 1;
            while (close < line_end && *close != ']') {
                close++;
            }
            if (close < line_end) {
                size_t sec_len = (size_t)(close - line_start - 1);
                in_settings = span_equals_ascii(line_start + 1, sec_len, CONFIG_CORE_SECTION);
            }
            continue;
        }

        if (!in_settings) {
            continue;
        }

        /* Find the '=' separator. Lines without one are ignored. */
        const char *eq = line_start;
        while (eq < line_end && *eq != '=') {
            eq++;
        }
        if (eq >= line_end) {
            continue;
        }

        /* Trim trailing whitespace from the key. */
        const char *key = line_start;
        size_t key_len = (size_t)(eq - line_start);
        while (key_len > 0 && (key[key_len - 1] == ' ' || key[key_len - 1] == '\t')) {
            key_len--;
        }
        if (key_len == 0) {
            continue;
        }

        /* Trim surrounding whitespace from the value. */
        const char *val = eq + 1;
        while (val < line_end && (*val == ' ' || *val == '\t')) {
            val++;
        }
        size_t val_len = (size_t)(line_end - val);
        while (val_len > 0 && (val[val_len - 1] == ' ' || val[val_len - 1] == '\t')) {
            val_len--;
        }

        /* Copy to null-terminated buffers so the callback receives plain C strings. */
        char key_buf[CONFIG_CORE_MAX_KEY];
        char val_buf[CONFIG_CORE_MAX_VALUE];
        if (key_len >= sizeof(key_buf)) {
            key_len = sizeof(key_buf) - 1;
        }
        if (val_len >= sizeof(val_buf)) {
            val_len = sizeof(val_buf) - 1;
        }
        memcpy(key_buf, key, key_len);
        key_buf[key_len] = '\0';
        memcpy(val_buf, val, val_len);
        val_buf[val_len] = '\0';

        cb(key_buf, val_buf, user);
    }

    return true;
}

/* ==== Growable buffer ==== */

void config_core_buf_init(config_core_buf_t *b) {
    if (b == NULL) {
        return;
    }
    b->data = NULL;
    b->length = 0;
    b->capacity = 0;
}

void config_core_buf_free(config_core_buf_t *b) {
    if (b == NULL) {
        return;
    }
    if (b->data != NULL) {
        LocalFree(b->data);
    }
    b->data = NULL;
    b->length = 0;
    b->capacity = 0;
}

/**
 * @brief Ensure the buffer can hold at least min_capacity bytes, doubling as needed.
 * @return true on success; false on allocation failure (buffer left untouched).
 */
static bool config_core_buf_reserve(config_core_buf_t *b, size_t min_capacity) {
    if (b->capacity >= min_capacity) {
        return true;
    }

    size_t new_cap = b->capacity == 0 ? CONFIG_CORE_BUF_INITIAL_CAPACITY : b->capacity;
    while (new_cap < min_capacity) {
        /* Guard against overflow on 32-bit builds. */
        if (new_cap > SIZE_MAX / 2) {
            new_cap = min_capacity;
            break;
        }
        new_cap *= 2;
    }

    char *new_data = (char *)LocalAlloc(LMEM_FIXED, new_cap);
    if (new_data == NULL) {
        return false;
    }
    if (b->data != NULL) {
        if (b->length > 0) {
            memcpy(new_data, b->data, b->length);
        }
        LocalFree(b->data);
    }
    b->data = new_data;
    b->capacity = new_cap;
    return true;
}

bool config_core_buf_append(config_core_buf_t *b, const char *fmt, ...) {
    if (b == NULL || fmt == NULL) {
        return false;
    }

    /* First pass: determine how many bytes the formatted string needs. */
    va_list args_size;
    va_start(args_size, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args_size);
    va_end(args_size);
    if (needed < 0) {
        return false;
    }

    /* Reserve space for the new content plus a trailing null we overwrite later. */
    size_t required = b->length + (size_t)needed + 1;
    if (required < b->length) {
        /* Overflow: cannot satisfy request. */
        return false;
    }
    if (!config_core_buf_reserve(b, required)) {
        return false;
    }

    /* Second pass: format directly into the reserved area. */
    va_list args_write;
    va_start(args_write, fmt);
    int written = vsnprintf(b->data + b->length, b->capacity - b->length, fmt, args_write);
    va_end(args_write);
    if (written < 0) {
        return false;
    }
    b->length += (size_t)written;
    return true;
}

bool config_core_buf_write_file(const config_core_buf_t *b, const wchar_t *path) {
    if (b == NULL || path == NULL) {
        return false;
    }

    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool ok = true;
    if (b->length > 0) {
        DWORD written = 0;
        if (!WriteFile(hFile, b->data, (DWORD)b->length, &written, NULL)) {
            ok = false;
        } else if (written != (DWORD)b->length) {
            ok = false;
        }
    }

    CloseHandle(hFile);
    return ok;
}
