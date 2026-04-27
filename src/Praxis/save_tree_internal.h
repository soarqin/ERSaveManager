#pragma once
/**
 * @file save_tree_internal.h
 * @brief Internal shared declarations for save tree modules.
 */

#include "save_tree.h"

#include <stdbool.h>
#include <stddef.h>

#include <windows.h>
#include <commctrl.h>

typedef struct save_item_s {
    wchar_t relative_path[MAX_PATH]; /* Relative to tree root */
    bool is_directory;
    bool is_readonly;
    FILETIME last_write_time;
} save_item_t;

struct save_tree_s {
    HWND hwnd;                  /* WC_TREEVIEW control */
    wchar_t root_path[MAX_PATH];
    save_item_t *items;         /* Dynamic array */
    size_t item_count;
    size_t item_capacity;
    /* Drag state */
    bool dragging;
    HTREEITEM drag_src;
    HIMAGELIST drag_image;
    /* System icon indices for cached lookups (resolved lazily). */
    int folder_icon_idx;        /* Cached generic folder icon index, -1 if unresolved */
    save_tree_sort_mode_t sort_mode;
};

void save_tree_make_display_name(const wchar_t *leaf, bool is_directory,
    bool is_readonly, wchar_t *out, size_t out_chars);
HIMAGELIST save_tree_get_system_image_list(void);
int save_tree_resolve_icon_index(const wchar_t *name, bool is_directory);
bool save_tree_append_item(save_tree_t *t, const wchar_t *relative_path, bool is_directory,
    bool is_readonly, const FILETIME *last_write_time, size_t *out_index);
bool save_tree_build_full_path(const save_tree_t *t, const wchar_t *relpath, wchar_t *out, size_t out_chars);
bool save_tree_get_item_info(const save_tree_t *t, HTREEITEM item, size_t *out_index, save_item_t *out_value);
bool save_tree_get_parent_relpath(const wchar_t *relpath, wchar_t *out, size_t out_chars);
void save_tree_collect_expanded_paths(save_tree_t *t, HTREEITEM hitem,
    wchar_t (*out_paths)[MAX_PATH], size_t out_capacity, size_t *count);
void save_tree_walk_dir(save_tree_t *t, const wchar_t *dir_path, const wchar_t *rel_prefix,
    HTREEITEM parent_item, int depth);
void save_tree_end_drag(save_tree_t *t);
LRESULT CALLBACK save_tree_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR subclass_id, DWORD_PTR ref_data);
