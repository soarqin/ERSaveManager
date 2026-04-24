/**
 * @file locale_core.c
 * @brief Generic locale infrastructure implementation.
 * @details Implements system-language detection driven by a caller-supplied language
 *          code array (BCP-47 tags) and stores the currently selected locale index.
 *          No string tables or enumerations live here; those are owned by each
 *          application's own locale module.
 */

#include "locale_core.h"

#include <windows.h>

/* File-local storage for the currently selected locale index. Apps interact
 * with it exclusively through locale_core_set_current / locale_core_get_current. */
static int g_current_locale = 0;

/**
 * @brief Case-insensitive comparison of two wide strings.
 */
static int wcsicmp_local(const wchar_t *a, const wchar_t *b) {
    return CompareStringOrdinal(a, -1, b, -1, TRUE) - CSTR_EQUAL;
}

int locale_core_detect_system_language(int default_index, const wchar_t **language_codes, int count) {
    if (language_codes == NULL || count <= 0) {
        return default_index;
    }

    /* Resolve the user's default UI locale (BCP-47 tag, e.g. "en-US", "zh-CN"). */
    wchar_t locale_name[LOCALE_NAME_MAX_LENGTH];
    ZeroMemory(locale_name, sizeof(locale_name));
    if (GetUserDefaultLocaleName(locale_name, LOCALE_NAME_MAX_LENGTH) == 0) {
        return default_index;
    }

    /* Pass 1: progressively trim trailing subtags and look for an exact match.
     * Example: "zh-Hans-CN" -> "zh-Hans" -> "zh". */
    wchar_t candidate[LOCALE_NAME_MAX_LENGTH];
    lstrcpynW(candidate, locale_name, LOCALE_NAME_MAX_LENGTH);
    while (candidate[0] != L'\0') {
        for (int i = 0; i < count; i++) {
            if (wcsicmp_local(candidate, language_codes[i]) == 0) {
                return i;
            }
        }
        wchar_t *last_dash = wcsrchr(candidate, L'-');
        if (last_dash == NULL) {
            break;
        }
        *last_dash = L'\0';
    }

    /* Pass 2: synthesise a primary-language + script tag (e.g. "zh-CN" -> "zh-Hans")
     * using the script associated with the user's locale. This lets callers supply
     * script-qualified codes like "zh-Hans" / "zh-Hant" without caring about regions. */
    wchar_t scripts[64];
    ZeroMemory(scripts, sizeof(scripts));
    if (GetLocaleInfoEx(locale_name, LOCALE_SSCRIPTS, scripts, (int)(sizeof(scripts) / sizeof(scripts[0]))) > 0) {
        /* LOCALE_SSCRIPTS returns a semicolon-separated list; keep the primary script only. */
        wchar_t *semi = wcschr(scripts, L';');
        if (semi != NULL) {
            *semi = L'\0';
        }
        if (scripts[0] != L'\0') {
            const wchar_t *dash = wcschr(locale_name, L'-');
            size_t primary_len = dash ? (size_t)(dash - locale_name) : wcslen(locale_name);
            size_t script_len = wcslen(scripts);
            if (primary_len > 0 && primary_len + 1 + script_len < LOCALE_NAME_MAX_LENGTH) {
                wchar_t combined[LOCALE_NAME_MAX_LENGTH];
                wcsncpy(combined, locale_name, primary_len);
                combined[primary_len] = L'-';
                lstrcpynW(combined + primary_len + 1, scripts,
                          LOCALE_NAME_MAX_LENGTH - (int)primary_len - 1);
                for (int i = 0; i < count; i++) {
                    if (wcsicmp_local(combined, language_codes[i]) == 0) {
                        return i;
                    }
                }
            }
        }
    }

    return default_index;
}

void locale_core_set_current(int index) {
    g_current_locale = index;
}

int locale_core_get_current(void) {
    return g_current_locale;
}
