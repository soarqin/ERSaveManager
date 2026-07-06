/**
 * @file save_discovery.c
 * @brief Elden Ring save file discovery helpers
 * @details Implements the shared rules for detecting candidate save files
 *          from either a Steam ID parent directory or a Steam ID directory.
 */

#include "save_discovery.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

#include <windows.h>
#include <shlwapi.h>

#define ER_MIN_STEAM_ID 0x10000000000000ULL
#define STEAM_ID_MAX_DIGITS 20

static bool string_fits(const wchar_t *text, size_t out_chars) {
    return text && out_chars > 0 && (size_t)lstrlenW(text) < out_chars;
}

static bool copy_string_checked(wchar_t *out, size_t out_chars, const wchar_t *text) {
    if (!out || !string_fits(text, out_chars)) {
        return false;
    }
    lstrcpyW(out, text);
    return true;
}

static void trim_trailing_separators(wchar_t *path) {
    size_t len;

    if (!path) {
        return;
    }

    len = (size_t)lstrlenW(path);
    while (len > 0 && (path[len - 1] == L'\\' || path[len - 1] == L'/')) {
        path[len - 1] = L'\0';
        len--;
    }
}

static bool copy_root_basename(const wchar_t *root_path, wchar_t *out, size_t out_chars) {
    wchar_t path_copy[MAX_PATH];
    const wchar_t *base_name;

    if (!copy_string_checked(path_copy, MAX_PATH, root_path)) {
        return false;
    }

    trim_trailing_separators(path_copy);
    base_name = PathFindFileNameW(path_copy);
    if (!base_name || base_name[0] == L'\0') {
        return false;
    }

    return copy_string_checked(out, out_chars, base_name);
}

static bool has_path_separator(const wchar_t *text) {
    return text && (wcschr(text, L'\\') || wcschr(text, L'/'));
}

static bool append_path_component_checked(wchar_t *path, size_t path_chars, const wchar_t *component) {
    if (!path || !component || component[0] == L'\0' || has_path_separator(component)) {
        return false;
    }
    if (!PathAppendW(path, component)) {
        return false;
    }
    return (size_t)lstrlenW(path) < path_chars;
}

bool save_discovery_is_steam_id_name(const wchar_t *name) {
    wchar_t *endptr;
    uint64_t steam_id;
    size_t len;

    if (!name || name[0] == L'\0') {
        return false;
    }

    len = (size_t)lstrlenW(name);
    if (len == 0 || len > STEAM_ID_MAX_DIGITS) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        if (name[i] < L'0' || name[i] > L'9') {
            return false;
        }
    }

    errno = 0;
    steam_id = wcstoull(name, &endptr, 10);
    return errno != ERANGE && endptr && *endptr == L'\0' && steam_id >= ER_MIN_STEAM_ID;
}

bool save_discovery_root_is_steam_id_dir(const wchar_t *root_path,
                                         wchar_t *steam_id_out,
                                         size_t steam_id_chars) {
    wchar_t base_name[32];

    if (!copy_root_basename(root_path, base_name, 32)) {
        return false;
    }
    if (!save_discovery_is_steam_id_name(base_name)) {
        return false;
    }

    if (steam_id_out && !copy_string_checked(steam_id_out, steam_id_chars, base_name)) {
        return false;
    }
    return true;
}

bool save_discovery_is_candidate_save_file(const WIN32_FIND_DATAW *find_data) {
    uint64_t file_size;
    const wchar_t *ext;

    if (!find_data || (find_data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }

    ext = PathFindExtensionW(find_data->cFileName);
    if (ext && lstrcmpiW(ext, L".bak") == 0) {
        return false;
    }

    file_size = ((uint64_t)find_data->nFileSizeHigh << 32) | (uint64_t)find_data->nFileSizeLow;
    return file_size == ER_SAVE_EXPECTED_FILE_SIZE;
}

bool save_discovery_make_display_name(const wchar_t *steam_id,
                                      const wchar_t *file_name,
                                      bool filename_only,
                                      wchar_t *out,
                                      size_t out_chars) {
    size_t steam_id_len;
    size_t delimiter_len;
    size_t file_name_len;

    if (!out || out_chars == 0 || !file_name || file_name[0] == L'\0') {
        return false;
    }

    if (filename_only) {
        return copy_string_checked(out, out_chars, file_name);
    }

    if (!save_discovery_is_steam_id_name(steam_id)) {
        return false;
    }

    steam_id_len = (size_t)lstrlenW(steam_id);
    delimiter_len = (size_t)lstrlenW(ER_SAVE_SELECTION_DELIMITER);
    file_name_len = (size_t)lstrlenW(file_name);
    if (steam_id_len + delimiter_len + file_name_len >= out_chars) {
        return false;
    }

    lstrcpyW(out, steam_id);
    lstrcatW(out, ER_SAVE_SELECTION_DELIMITER);
    lstrcatW(out, file_name);
    return true;
}

bool save_discovery_resolve_selection(const wchar_t *root_path,
                                      const wchar_t *selection,
                                      wchar_t *save_path_out,
                                      size_t save_path_chars,
                                      wchar_t *steam_id_out,
                                      size_t steam_id_chars) {
    wchar_t steam_id[32];
    wchar_t save_path[MAX_PATH];
    const wchar_t *file_name;
    const wchar_t *delimiter;
    size_t steam_id_len;

    if (!root_path || !selection || !save_path_out || !steam_id_out ||
        save_path_chars == 0 || steam_id_chars == 0 || selection[0] == L'\0') {
        return false;
    }

    if (!copy_string_checked(save_path, MAX_PATH, root_path)) {
        return false;
    }

    if (save_discovery_root_is_steam_id_dir(root_path, steam_id, 32)) {
        file_name = selection;
        if (has_path_separator(file_name)) {
            return false;
        }
    } else {
        delimiter = wcsstr(selection, ER_SAVE_SELECTION_DELIMITER);
        if (!delimiter) {
            return false;
        }

        steam_id_len = (size_t)(delimiter - selection);
        if (steam_id_len == 0 || steam_id_len >= 32) {
            return false;
        }
        CopyMemory(steam_id, selection, steam_id_len * sizeof(wchar_t));
        steam_id[steam_id_len] = L'\0';
        if (!save_discovery_is_steam_id_name(steam_id)) {
            return false;
        }

        file_name = delimiter + lstrlenW(ER_SAVE_SELECTION_DELIMITER);
        if (!file_name || file_name[0] == L'\0' || has_path_separator(file_name)) {
            return false;
        }

        if (!append_path_component_checked(save_path, MAX_PATH, steam_id)) {
            return false;
        }
    }

    if (!append_path_component_checked(save_path, MAX_PATH, file_name)) {
        return false;
    }
    if (!copy_string_checked(save_path_out, save_path_chars, save_path) ||
        !copy_string_checked(steam_id_out, steam_id_chars, steam_id)) {
        return false;
    }

    return true;
}
