/**
 * @file config_core.h
 * @brief Generic INI config utility toolkit for ERSaveManager applications.
 * @details Provides path resolution, parsing, and write helpers. Each app owns
 *          its own config_t struct, keys, and per-field dispatch logic. This
 *          toolkit only supplies reusable primitives (executable directory
 *          resolution, integer parsing, UTF-8 to wide conversion, a generic
 *          [Settings] section parser driven by a callback, and a growable
 *          output buffer).
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

/**
 * @brief Builds <exe-dir>\<app_ini_name> using GetModuleFileNameW and PathRemoveFileSpecW / PathAppendW.
 * @param out          Destination buffer for the resolved path (wide, null-terminated).
 * @param out_chars    Capacity of out in wchar_t units (including space for the null terminator).
 * @param app_ini_name File name to append to the executable's directory (e.g. L"ERSaveManager.ini").
 * @return true on success; false if any parameter is invalid or the Win32 call fails.
 */
bool config_core_get_app_ini_path(wchar_t *out, size_t out_chars, const wchar_t *app_ini_name);

/**
 * @brief Converts a null-terminated decimal string to int, returning defval on parse failure.
 * @param value  Null-terminated input string. Leading whitespace is accepted.
 * @param defval Value returned when value is NULL, empty, or no digits can be consumed.
 * @return Parsed integer, clamped to the INT range, or defval on failure.
 */
int config_core_parse_int(const char *value, int defval);

/**
 * @brief Converts a null-terminated UTF-8 string to a wide string.
 * @param out        Destination buffer.
 * @param out_chars  Capacity of out in wchar_t units (must include room for the null terminator).
 * @param utf8_value Null-terminated UTF-8 input. NULL is treated as an empty string.
 * @return true when the buffer was populated (including the trailing null); false when the
 *         buffer is too small or the conversion failed. The output is always null-terminated
 *         on return.
 */
bool config_core_store_wide_value(wchar_t *out, size_t out_chars, const char *utf8_value);

/**
 * @brief Callback invoked for every key=value pair inside the [Settings] section.
 * @param key   Null-terminated key name (trimmed of surrounding whitespace).
 * @param value Null-terminated value (trimmed of surrounding whitespace).
 * @param user  Opaque pointer supplied to config_core_parse_ini.
 */
typedef void (*config_core_kv_callback)(const char *key, const char *value, void *user);

/**
 * @brief Parses a UTF-8 INI buffer, calling cb for each key=value pair inside [Settings].
 * @details Strips a UTF-8 BOM, ignores blank lines, recognises ';' and '#' comment markers,
 *          and trims whitespace from keys and values. Non-[Settings] sections are skipped.
 * @param buffer Raw INI bytes (does not need to be null-terminated).
 * @param length Number of bytes in buffer.
 * @param cb     Callback invoked for each key/value inside [Settings].
 * @param user   Opaque pointer forwarded to cb.
 * @return true on success; false if buffer or cb is NULL.
 */
bool config_core_parse_ini(const char *buffer, size_t length, config_core_kv_callback cb, void *user);

/**
 * @brief Growable UTF-8 byte buffer used to build INI output before a single WriteFile call.
 * @details Backed by LocalAlloc/LocalFree. data may be NULL before the first append; length
 *          and capacity are always consistent. The buffer stores raw bytes only and does not
 *          keep a trailing null terminator beyond length.
 */
typedef struct {
    char *data;       /* Buffer storage (LocalAlloc); may be NULL before first append */
    size_t length;    /* Number of bytes currently stored */
    size_t capacity;  /* Allocated capacity in bytes */
} config_core_buf_t;

/**
 * @brief Initialises an empty buffer (no allocation performed yet).
 */
void config_core_buf_init(config_core_buf_t *b);

/**
 * @brief Releases the buffer's backing storage and resets its state.
 */
void config_core_buf_free(config_core_buf_t *b);

/**
 * @brief Appends formatted UTF-8 text to the buffer, growing it as needed.
 * @param b   Buffer (must have been initialised via config_core_buf_init).
 * @param fmt printf-style format string; variadic arguments follow.
 * @return true on success; false on allocation or formatting failure.
 */
bool config_core_buf_append(config_core_buf_t *b, const char *fmt, ...);

/**
 * @brief Writes the buffer's contents to path in a single WriteFile call (CREATE_ALWAYS).
 * @return true on success; false if the file cannot be created or the write fails.
 */
bool config_core_buf_write_file(const config_core_buf_t *b, const wchar_t *path);

/**
 * @brief Callback invoked when a new section header is encountered.
 * @param section Null-terminated section name (e.g. "Settings", "GameProfile:1").
 * @param user    Opaque pointer supplied to config_core_parse_ini_ex.
 */
typedef void (*config_core_section_callback)(const char *section, void *user);

/**
 * @brief Extended INI parser supporting multiple sections with section-change notification.
 * @details Calls section_cb when a [SectionName] header is found (for ALL sections).
 *          Calls kv_cb for every key=value pair in the current section (for ALL sections).
 *          Unlike config_core_parse_ini, does NOT restrict to [Settings] only.
 *          If section_cb is NULL, still parses all sections but only calls kv_cb.
 *          If kv_cb is NULL, only calls section_cb.
 * @param buffer     Raw INI bytes.
 * @param length     Number of bytes in buffer.
 * @param section_cb Called when a section header is encountered (may be NULL).
 * @param kv_cb      Called for each key=value pair in any section (may be NULL).
 * @param user       Opaque pointer forwarded to both callbacks.
 * @return true on success; false if buffer is NULL.
 */
bool config_core_parse_ini_ex(const char *buffer, size_t length,
                               config_core_section_callback section_cb,
                               config_core_kv_callback kv_cb,
                               void *user);
