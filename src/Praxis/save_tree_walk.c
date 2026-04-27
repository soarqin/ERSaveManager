/**
 * @file save_tree_walk.c
 * @brief Filesystem walking helpers for the Praxis save tree.
 */

#include "save_tree_internal.h"

#include <stdbool.h>
#include <stdlib.h>

#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>

#define SAVE_TREE_MAX_DEPTH 16

typedef struct walk_entry_s {
    wchar_t name[MAX_PATH];
    DWORD attributes;
    FILETIME last_write_time;
} walk_entry_t;

static save_tree_sort_mode_t g_walk_sort_mode = SAVE_TREE_SORT_NAME_ASC;

static int __cdecl compare_walk_entries(const void *lhs, const void *rhs) {
    const walk_entry_t *left = (const walk_entry_t *)lhs;
    const walk_entry_t *right = (const walk_entry_t *)rhs;
    bool left_is_dir = (left->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    bool right_is_dir = (right->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    int cmp;

    if (left_is_dir != right_is_dir) return left_is_dir ? -1 : 1;
    if (left_is_dir) return lstrcmpW(left->name, right->name);

    switch (g_walk_sort_mode) {
    case SAVE_TREE_SORT_NAME_DESC:
        return -lstrcmpW(left->name, right->name);
    case SAVE_TREE_SORT_MODIFIED_ASC:
        cmp = CompareFileTime(&left->last_write_time, &right->last_write_time);
        return cmp != 0 ? cmp : lstrcmpW(left->name, right->name);
    case SAVE_TREE_SORT_MODIFIED_DESC:
        cmp = CompareFileTime(&left->last_write_time, &right->last_write_time);
        return cmp != 0 ? -cmp : lstrcmpW(left->name, right->name);
    case SAVE_TREE_SORT_NAME_ASC:
    default:
        return lstrcmpW(left->name, right->name);
    }
}

void save_tree_collect_expanded_paths(save_tree_t *t, HTREEITEM hitem,
    wchar_t (*out_paths)[MAX_PATH], size_t out_capacity, size_t *count) {
    while (t && hitem && out_paths && count && *count < out_capacity) {
        TVITEMW tv_item = {0};
        HTREEITEM child;
        size_t index;

        tv_item.hItem = hitem;
        tv_item.mask = TVIF_PARAM | TVIF_STATE;
        tv_item.stateMask = TVIS_EXPANDED;
        if (TreeView_GetItem(t->hwnd, &tv_item)) {
            index = (size_t)(uintptr_t)tv_item.lParam;
            if ((tv_item.state & TVIS_EXPANDED) && index < t->item_count) {
                const save_item_t *tree_item = &t->items[index];
                if (tree_item->is_directory) {
                    lstrcpynW(out_paths[*count], tree_item->relative_path, MAX_PATH);
                    (*count)++;
                }
            }
            child = TreeView_GetChild(t->hwnd, hitem);
            if (child) save_tree_collect_expanded_paths(t, child, out_paths, out_capacity, count);
        }
        hitem = TreeView_GetNextSibling(t->hwnd, hitem);
    }
}

void save_tree_walk_dir(save_tree_t *t, const wchar_t *dir_path, const wchar_t *rel_prefix,
    HTREEITEM parent_item, int depth) {
    wchar_t search[MAX_PATH];
    WIN32_FIND_DATAW find_data;
    HANDLE find_handle;
    walk_entry_t *entries = NULL;
    size_t entry_count = 0;
    size_t entry_capacity = 0;

    if (!t || !dir_path || !rel_prefix || depth > SAVE_TREE_MAX_DEPTH) return;
    lstrcpyW(search, dir_path);
    if (!PathAppendW(search, L"*")) return;

    find_handle = FindFirstFileW(search, &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) return;

    do {
        walk_entry_t *new_entries;

        if (find_data.cFileName[0] == L'.') continue;
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
        if (entry_count >= entry_capacity) {
            size_t new_capacity = entry_capacity == 0 ? 16 : entry_capacity * 2;
            new_entries = LocalAlloc(LMEM_FIXED, new_capacity * sizeof(walk_entry_t));
            if (!new_entries) break;
            if (entries && entry_count > 0) CopyMemory(new_entries, entries, entry_count * sizeof(walk_entry_t));
            if (entries) LocalFree(entries);
            entries = new_entries;
            entry_capacity = new_capacity;
        }
        lstrcpynW(entries[entry_count].name, find_data.cFileName, MAX_PATH);
        entries[entry_count].attributes = find_data.dwFileAttributes;
        entries[entry_count].last_write_time = find_data.ftLastWriteTime;
        entry_count++;
    } while (FindNextFileW(find_handle, &find_data));

    FindClose(find_handle);
    if (!entries) return;
    g_walk_sort_mode = t->sort_mode;
    qsort(entries, entry_count, sizeof(walk_entry_t), compare_walk_entries);

    for (size_t i = 0; i < entry_count; i++) {
        wchar_t rel_path[MAX_PATH];
        wchar_t full_path[MAX_PATH];
        wchar_t display_name[MAX_PATH];
        bool is_directory = (entries[i].attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        HTREEITEM inserted = NULL;
        size_t index;
        TVINSERTSTRUCTW insert = {0};
        int icon_index;

        if (rel_prefix[0] != L'\0') {
            lstrcpyW(rel_path, rel_prefix);
            if (!PathAppendW(rel_path, entries[i].name)) continue;
        } else {
            lstrcpyW(rel_path, entries[i].name);
        }
        lstrcpyW(full_path, dir_path);
        if (!PathAppendW(full_path, entries[i].name)) continue;
        if (!save_tree_append_item(t, rel_path, is_directory,
            (entries[i].attributes & FILE_ATTRIBUTE_READONLY) != 0,
            &entries[i].last_write_time, &index)) break;

        save_tree_make_display_name(entries[i].name, is_directory,
            (entries[i].attributes & FILE_ATTRIBUTE_READONLY) != 0, display_name, MAX_PATH);
        icon_index = save_tree_resolve_icon_index(entries[i].name, is_directory);
        if (t->hwnd) {
            insert.hParent = parent_item;
            insert.hInsertAfter = TVI_LAST;
            insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
            insert.item.pszText = display_name;
            insert.item.lParam = (LPARAM)(uintptr_t)index;
            insert.item.iImage = icon_index;
            insert.item.iSelectedImage = icon_index;
            inserted = TreeView_InsertItem(t->hwnd, &insert);
        }
        if (is_directory) save_tree_walk_dir(t, full_path, rel_path, inserted, depth + 1);
    }

    LocalFree(entries);
}
