/**
 * @file save_tree.h
 * @brief TreeView-based save library widget for Praxis.
 * @details Manages a WC_TREEVIEW control backed by a filesystem directory.
 *          lParam values are ALWAYS array indices, NEVER heap pointers.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <windows.h>
#include <commctrl.h>

typedef struct save_tree_s save_tree_t;

save_tree_t *save_tree_create(HWND parent, HINSTANCE hinst, int id);
void save_tree_destroy(save_tree_t *t);
bool save_tree_set_root(save_tree_t *t, const wchar_t *root_path);
void save_tree_refresh(save_tree_t *t);
/**
 * @brief Refresh the tree preserving the selection by relative path.
 * @details Captures the currently selected item's relative path before
 *          refreshing. After refresh, finds the matching item and re-selects.
 *          If not found, walks up by trimming trailing path components until
 *          a match is found or falls back to the wrapper root (empty relpath).
 *          Ends any active drag operation before refreshing.
 * @param t Tree widget instance.
 */
void save_tree_refresh_preserve_selection(save_tree_t *t);
bool save_tree_get_selected_path(const save_tree_t *t, wchar_t *out, size_t out_chars);
/**
 * @brief Select an item by its absolute file path.
 * @details Converts the absolute path to a relative path (under t->root_path),
 *          finds the matching item in the tree, selects it via TreeView_SelectItem,
 *          and ensures it is visible via TreeView_EnsureVisible.
 *          Returns false if full_path is not under t->root_path or no matching
 *          item exists in the tree.
 * @param t Tree widget instance.
 * @param full_path Absolute file or directory path.
 * @return true on success, false otherwise.
 */
bool save_tree_select_full_path(save_tree_t *t, const wchar_t *full_path);
/**
 * @brief Select the previous or next file in the selected item's directory.
 * @details Files are ordered the same way they appear in the tree. If the
 *          current selection is a file, cycles among files in that file's
 *          parent directory. If the current selection is a directory or the
 *          root wrapper, selects the first/last file directly inside it.
 * @param t Tree widget instance.
 * @param direction Negative selects previous, positive selects next.
 * @return true when a sibling file was selected, false otherwise.
 */
bool save_tree_select_sibling_file(save_tree_t *t, int direction);
/**
 * @brief Get the directory path of the currently selected item.
 * @details If the selection is a directory, returns its full path. If the
 *          selection is a file, returns the parent directory's full path.
 *          If no selection, or the wrapper root is selected (relative_path
 *          is empty), returns the tree root path.
 * @param t Tree widget instance.
 * @param out Buffer to receive the full path (caller must allocate).
 * @param out_chars Capacity of out (should be MAX_PATH).
 * @return true on success, false if t is invalid or path cannot be built.
 */
bool save_tree_get_selected_dir(const save_tree_t *t, wchar_t *out, size_t out_chars);
HWND save_tree_get_hwnd(const save_tree_t *t);
bool save_tree_handle_notify(save_tree_t *t, LPNMHDR pnm, LRESULT *out_result);

/* Headless operations for selftest subcommands */
bool save_tree_rename(save_tree_t *t, const wchar_t *old_relpath, const wchar_t *new_name);
bool save_tree_delete(save_tree_t *t, const wchar_t *relpath);
bool save_tree_new_folder(save_tree_t *t, const wchar_t *parent_relpath, const wchar_t *name);
bool save_tree_move(save_tree_t *t, const wchar_t *src_relpath, const wchar_t *dst_parent_relpath);
int save_tree_item_count(const save_tree_t *t);
