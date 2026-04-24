/**
 * @file locale_core.h
 * @brief Generic locale infrastructure shared between applications.
 * @details Provides system language detection and current locale index storage.
 *          Each application maintains its own string table (STR_* enum and locale_strings[][]).
 */

#pragma once

#include <wchar.h>

/**
 * @brief Detects system UI language and returns its index in the caller's language codes array.
 * @param default_index Index to return when no match is found.
 * @param language_codes Array of BCP-47 language code strings (e.g. {L"en", L"fr", L"zh-Hans"}).
 * @param count Number of entries in language_codes.
 * @return Index of the matched language, or default_index if none matched.
 */
int locale_core_detect_system_language(int default_index, const wchar_t **language_codes, int count);

/**
 * @brief Sets the current locale index.
 * @param index The locale index to set as current.
 */
void locale_core_set_current(int index);

/**
 * @brief Gets the current locale index.
 * @return The current locale index.
 */
int locale_core_get_current(void);
