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
HWND save_tree_get_hwnd(const save_tree_t *t);
bool save_tree_handle_notify(save_tree_t *t, LPNMHDR pnm, LRESULT *out_result);

/* Headless operations for selftest subcommands */
bool save_tree_rename(save_tree_t *t, const wchar_t *old_relpath, const wchar_t *new_name);
bool save_tree_delete(save_tree_t *t, const wchar_t *relpath);
bool save_tree_new_folder(save_tree_t *t, const wchar_t *parent_relpath, const wchar_t *name);
bool save_tree_move(save_tree_t *t, const wchar_t *src_relpath, const wchar_t *dst_parent_relpath);
int save_tree_item_count(const save_tree_t *t);
