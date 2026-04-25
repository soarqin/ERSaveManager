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
};

void save_tree_make_display_name(const wchar_t *leaf, bool is_directory,
    wchar_t *out, size_t out_chars);
bool save_tree_build_full_path(const save_tree_t *t, const wchar_t *relpath, wchar_t *out, size_t out_chars);
bool save_tree_get_item_info(const save_tree_t *t, HTREEITEM item, size_t *out_index, save_item_t *out_value);
bool save_tree_get_parent_relpath(const wchar_t *relpath, wchar_t *out, size_t out_chars);
void save_tree_end_drag(save_tree_t *t);
LRESULT CALLBACK save_tree_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR subclass_id, DWORD_PTR ref_data);
