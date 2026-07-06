/**
 * @file save_discovery.h
 * @brief Elden Ring save file discovery helpers
 * @details Provides validation, display-name formatting, and selection
 *          resolution helpers for ERSaveManager's save-file dropdown.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <windows.h>

#define ER_SAVE_EXPECTED_FILE_SIZE 28967888ULL
#define ER_SAVE_SELECTION_DELIMITER L" / "

/**
 * @brief Check whether a directory name looks like a decimal Steam ID.
 * @param name Directory name to validate.
 * @return true when the name is a decimal Steam ID, false otherwise.
 */
bool save_discovery_is_steam_id_name(const wchar_t *name);

/**
 * @brief Check whether the selected root path itself is a Steam ID directory.
 * @param root_path Selected save root path.
 * @param steam_id_out Optional output buffer receiving the Steam ID directory name.
 * @param steam_id_chars Number of wchar_t elements in @p steam_id_out.
 * @return true when @p root_path ends in a valid Steam ID directory name.
 */
bool save_discovery_root_is_steam_id_dir(const wchar_t *root_path,
                                         wchar_t *steam_id_out,
                                         size_t steam_id_chars);

/**
 * @brief Check whether a file-system entry is a candidate Elden Ring save file.
 * @param find_data Find data returned by FindFirstFileW/FindNextFileW.
 * @return true for regular files of the expected save size, excluding *.bak.
 */
bool save_discovery_is_candidate_save_file(const WIN32_FIND_DATAW *find_data);

/**
 * @brief Build the text shown in the save-file dropdown.
 * @param steam_id Steam ID directory name.
 * @param file_name Candidate save file name.
 * @param filename_only true to show only @p file_name; false to show "SteamID / file".
 * @param out Output buffer.
 * @param out_chars Number of wchar_t elements in @p out.
 * @return true on success, false if the formatted text does not fit.
 */
bool save_discovery_make_display_name(const wchar_t *steam_id,
                                      const wchar_t *file_name,
                                      bool filename_only,
                                      wchar_t *out,
                                      size_t out_chars);

/**
 * @brief Resolve a dropdown selection to a concrete save path and Steam ID.
 * @param root_path Selected root path. May be the Steam ID parent or the Steam ID directory.
 * @param selection Dropdown text selected by the user.
 * @param save_path_out Output buffer receiving the full save file path.
 * @param save_path_chars Number of wchar_t elements in @p save_path_out.
 * @param steam_id_out Output buffer receiving the owning Steam ID.
 * @param steam_id_chars Number of wchar_t elements in @p steam_id_out.
 * @return true on success, false when the selection is invalid or paths do not fit.
 */
bool save_discovery_resolve_selection(const wchar_t *root_path,
                                      const wchar_t *selection,
                                      wchar_t *save_path_out,
                                      size_t save_path_chars,
                                      wchar_t *steam_id_out,
                                      size_t steam_id_chars);
