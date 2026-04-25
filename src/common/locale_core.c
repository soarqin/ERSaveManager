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

    /* Pass 2: synthesise primary-language + script tags (e.g. "zh-CN" -> "zh-Hans")
     * using the scripts associated with the user's locale. LOCALE_SSCRIPTS returns
     * a SEMICOLON-DELIMITED list of scripts; for example, "zh-CN" returns
     * "Hani;Hans;" (Han ideographs first, then the specific script). We must try
     * EACH script in the list because the first token is often the parent script
     * (Hani) which does not match our codes (Hans / Hant). */
    wchar_t scripts[64];
    ZeroMemory(scripts, sizeof(scripts));
    if (GetLocaleInfoEx(locale_name, LOCALE_SSCRIPTS, scripts,
                        (int)(sizeof(scripts) / sizeof(scripts[0]))) > 0) {
        const wchar_t *dash = wcschr(locale_name, L'-');
        size_t primary_len = dash ? (size_t)(dash - locale_name) : wcslen(locale_name);
        if (primary_len > 0) {
            /* Walk each semicolon-delimited script token in turn. */
            wchar_t *token_start = scripts;
            while (token_start != NULL && *token_start != L'\0') {
                wchar_t *token_end = wcschr(token_start, L';');
                size_t token_len = token_end
                    ? (size_t)(token_end - token_start)
                    : wcslen(token_start);

                if (token_len > 0 &&
                    primary_len + 1 + token_len + 1 <= LOCALE_NAME_MAX_LENGTH) {
                    wchar_t combined[LOCALE_NAME_MAX_LENGTH];
                    wcsncpy(combined, locale_name, primary_len);
                    combined[primary_len] = L'-';
                    wcsncpy(combined + primary_len + 1, token_start, token_len);
                    combined[primary_len + 1 + token_len] = L'\0';

                    for (int i = 0; i < count; i++) {
                        if (wcsicmp_local(combined, language_codes[i]) == 0) {
                            return i;
                        }
                    }
                }

                if (token_end == NULL) {
                    break;
                }
                token_start = token_end + 1;
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
