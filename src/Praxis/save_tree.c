/**
 * @file save_tree.c
 * @brief TreeView-backed save library widget for Praxis.
 * @details Enumerates a rooted filesystem tree, exposes headless file operations,
 *          and handles TreeView rename, context menu, and drag-move behavior.
 */

#include "save_tree.h"

#include "locale.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>

#define SAVE_TREE_MAX_DEPTH 16
#define SAVE_TREE_MAX_EXPANDED_PATHS 256
#define ID_SAVE_TREE_NEW_FOLDER 50001
#define ID_SAVE_TREE_RENAME 50002
#define ID_SAVE_TREE_DELETE 50003

typedef struct save_item_s {
    wchar_t relative_path[MAX_PATH]; /* Relative to tree root */
    bool is_directory;
} save_item_t;

typedef struct walk_entry_s {
    wchar_t name[MAX_PATH];
    DWORD attributes;
} walk_entry_t;

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
};

static bool ensure_capacity(save_tree_t *t) {
    if (!t) {
        return false;
    }
    if (t->item_count < t->item_capacity) {
        return true;
    }

    size_t new_capacity = t->item_capacity == 0 ? 64 : t->item_capacity * 2;
    save_item_t *new_items = LocalAlloc(LMEM_FIXED, new_capacity * sizeof(save_item_t));
    if (!new_items) {
        return false;
    }

    if (t->items && t->item_count > 0) {
        CopyMemory(new_items, t->items, t->item_count * sizeof(save_item_t));
    }
    if (t->items) {
        LocalFree(t->items);
    }

    t->items = new_items;
    t->item_capacity = new_capacity;
    return true;
}

static bool build_full_path(const save_tree_t *t, const wchar_t *relpath, wchar_t *out, size_t out_chars) {
    size_t root_len;

    if (!t || !out || out_chars == 0 || t->root_path[0] == L'\0') {
        return false;
    }

    root_len = (size_t)lstrlenW(t->root_path);
    if (root_len >= out_chars) {
        return false;
    }

    lstrcpyW(out, t->root_path);
    if (relpath && relpath[0] != L'\0' && !PathAppendW(out, relpath)) {
        return false;
    }
    return true;
}

static bool get_item_info(const save_tree_t *t, HTREEITEM item, size_t *out_index, save_item_t *out_value) {
    TVITEMW tv_item = {0};
    size_t index;

    if (!t || !t->hwnd || !item) {
        return false;
    }

    tv_item.mask = TVIF_PARAM;
    tv_item.hItem = item;
    if (!TreeView_GetItem(t->hwnd, &tv_item)) {
        return false;
    }

    index = (size_t)(uintptr_t)tv_item.lParam;
    if (index >= t->item_count) {
        return false;
    }

    if (out_index) {
        *out_index = index;
    }
    if (out_value) {
        *out_value = t->items[index];
    }
    return true;
}

static bool get_parent_relpath(const wchar_t *relpath, wchar_t *out, size_t out_chars) {
    if (!out || out_chars == 0) {
        return false;
    }

    out[0] = L'\0';
    if (!relpath || relpath[0] == L'\0') {
        return true;
    }

    if ((size_t)lstrlenW(relpath) >= out_chars) {
        return false;
    }

    lstrcpyW(out, relpath);
    if (!PathRemoveFileSpecW(out)) {
        out[0] = L'\0';
    }
    return true;
}

static bool is_valid_name(const wchar_t *name) {
    static const wchar_t invalid_chars[] = L"\\/:*?\"<>|";

    if (!name || name[0] == L'\0' || lstrcmpW(name, L".") == 0 || lstrcmpW(name, L"..") == 0) {
        return false;
    }

    for (int i = 0; invalid_chars[i] != L'\0'; i++) {
        if (wcschr(name, invalid_chars[i])) {
            return false;
        }
    }

    return true;
}

static int __cdecl compare_walk_entries(const void *lhs, const void *rhs) {
    const walk_entry_t *left = (const walk_entry_t *)lhs;
    const walk_entry_t *right = (const walk_entry_t *)rhs;
    bool left_is_dir = (left->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    bool right_is_dir = (right->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    if (left_is_dir != right_is_dir) {
        return left_is_dir ? -1 : 1;
    }
    return lstrcmpW(left->name, right->name);
}

static void end_drag(save_tree_t *t) {
    if (!t) {
        return;
    }

    if (t->dragging) {
        if (t->hwnd) {
            TreeView_SelectDropTarget(t->hwnd, NULL);
        }
        if (GetCapture() == t->hwnd) {
            ReleaseCapture();
        }
        if (t->drag_image) {
            ImageList_DragLeave(t->hwnd);
            ImageList_EndDrag();
        }
    }

    if (t->drag_image) {
        ImageList_Destroy(t->drag_image);
        t->drag_image = NULL;
    }

    t->dragging = false;
    t->drag_src = NULL;
}

static bool append_item(save_tree_t *t, const wchar_t *relative_path, bool is_directory, size_t *out_index) {
    size_t index;

    if (!t || !relative_path || !ensure_capacity(t)) {
        return false;
    }

    index = t->item_count;
    lstrcpynW(t->items[index].relative_path, relative_path, MAX_PATH);
    t->items[index].is_directory = is_directory;
    t->item_count++;

    if (out_index) {
        *out_index = index;
    }
    return true;
}

static bool find_item_index_by_relpath(const save_tree_t *t, const wchar_t *relpath, size_t *out_index) {
    if (!t || !relpath) {
        return false;
    }

    for (size_t i = 0; i < t->item_count; i++) {
        if (lstrcmpW(t->items[i].relative_path, relpath) == 0) {
            if (out_index) {
                *out_index = i;
            }
            return true;
        }
    }

    return false;
}

static HTREEITEM find_hitem_by_lparam(HWND hwnd, HTREEITEM hitem, LPARAM target) {
    while (hitem) {
        TVITEMW tvi = {0};
        HTREEITEM child;
        HTREEITEM found;

        tvi.hItem = hitem;
        tvi.mask = TVIF_PARAM;
        if (TreeView_GetItem(hwnd, &tvi) && tvi.lParam == target) {
            return hitem;
        }

        child = TreeView_GetChild(hwnd, hitem);
        if (child) {
            found = find_hitem_by_lparam(hwnd, child, target);
            if (found) {
                return found;
            }
        }

        hitem = TreeView_GetNextSibling(hwnd, hitem);
    }

    return NULL;
}

/**
 * @brief Recursively collect relative_paths of all expanded directory items.
 * @details Performs a depth-first walk through TreeView items. Each expanded
 *          directory contributes its relative path to the output buffer until
 *          the provided capacity is reached.
 * @param t Save tree instance.
 * @param hitem Tree item to start from.
 * @param out_paths Destination array of relative paths.
 * @param out_capacity Number of available path slots in @p out_paths.
 * @param count Running number of captured paths.
 */
static void collect_expanded_paths(save_tree_t *t, HTREEITEM hitem,
    wchar_t (*out_paths)[MAX_PATH], size_t out_capacity, size_t *count) {
    while (t && hitem && out_paths && count && *count < out_capacity) {
        TVITEMW tvi = {0};
        HTREEITEM child;
        size_t index;

        tvi.hItem = hitem;
        tvi.mask = TVIF_PARAM | TVIF_STATE;
        tvi.stateMask = TVIS_EXPANDED;
        if (TreeView_GetItem(t->hwnd, &tvi)) {
            index = (size_t)(uintptr_t)tvi.lParam;
            if ((tvi.state & TVIS_EXPANDED) && index < t->item_count) {
                const save_item_t *item = &t->items[index];
                if (item->is_directory) {
                    lstrcpynW(out_paths[*count], item->relative_path, MAX_PATH);
                    (*count)++;
                }
            }

            child = TreeView_GetChild(t->hwnd, hitem);
            if (child) {
                collect_expanded_paths(t, child, out_paths, out_capacity, count);
            }
        }

        hitem = TreeView_GetNextSibling(t->hwnd, hitem);
    }
}

static bool choose_drop_parent(const save_tree_t *t, HTREEITEM target, wchar_t *dst_parent_relpath, size_t out_chars) {
    save_item_t item;

    if (!dst_parent_relpath || out_chars == 0) {
        return false;
    }

    dst_parent_relpath[0] = L'\0';
    if (!target) {
        return true;
    }

    if (!get_item_info(t, target, NULL, &item)) {
        return false;
    }

    if (item.is_directory) {
        if ((size_t)lstrlenW(item.relative_path) >= out_chars) {
            return false;
        }
        lstrcpyW(dst_parent_relpath, item.relative_path);
        return true;
    }

    return get_parent_relpath(item.relative_path, dst_parent_relpath, out_chars);
}

static bool create_unique_folder(save_tree_t *t, const wchar_t *parent_relpath) {
    wchar_t name[MAX_PATH];
    const wchar_t *base_name = praxis_locale_str(STR_PRAXIS_NEW_FOLDER);

    if (!t || !base_name) {
        return false;
    }

    if (save_tree_new_folder(t, parent_relpath, base_name)) {
        return true;
    }

    for (int suffix = 2; suffix <= 999; suffix++) {
        _snwprintf(name, MAX_PATH, L"%ls (%d)", base_name, suffix);
        name[MAX_PATH - 1] = L'\0';
        if (save_tree_new_folder(t, parent_relpath, name)) {
            return true;
        }
    }

    return false;
}

static void walk_dir(save_tree_t *t, const wchar_t *dir_path, const wchar_t *rel_prefix,
    HTREEITEM parent_item, int depth) {
    wchar_t search[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE handle;
    walk_entry_t *entries = NULL;
    size_t entry_count = 0;
    size_t entry_capacity = 0;

    if (!t || !dir_path || !rel_prefix || depth > SAVE_TREE_MAX_DEPTH) {
        return;
    }

    lstrcpyW(search, dir_path);
    if (!PathAppendW(search, L"*")) {
        return;
    }

    handle = FindFirstFileW(search, &fd);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        walk_entry_t *new_entries;

        if (fd.cFileName[0] == L'.') {
            continue;
        }
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            continue;
        }

        if (entry_count >= entry_capacity) {
            size_t new_capacity = entry_capacity == 0 ? 16 : entry_capacity * 2;
            new_entries = LocalAlloc(LMEM_FIXED, new_capacity * sizeof(walk_entry_t));
            if (!new_entries) {
                break;
            }
            if (entries && entry_count > 0) {
                CopyMemory(new_entries, entries, entry_count * sizeof(walk_entry_t));
            }
            if (entries) {
                LocalFree(entries);
            }
            entries = new_entries;
            entry_capacity = new_capacity;
        }

        lstrcpynW(entries[entry_count].name, fd.cFileName, MAX_PATH);
        entries[entry_count].attributes = fd.dwFileAttributes;
        entry_count++;
    } while (FindNextFileW(handle, &fd));

    FindClose(handle);

    if (!entries) {
        return;
    }

    qsort(entries, entry_count, sizeof(walk_entry_t), compare_walk_entries);

    for (size_t i = 0; i < entry_count; i++) {
        wchar_t rel_path[MAX_PATH];
        wchar_t full_path[MAX_PATH];
        bool is_dir = (entries[i].attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        HTREEITEM inserted = NULL;
        size_t index;
        TVINSERTSTRUCTW insert = {0};

        if (rel_prefix[0] != L'\0') {
            lstrcpyW(rel_path, rel_prefix);
            if (!PathAppendW(rel_path, entries[i].name)) {
                continue;
            }
        } else {
            lstrcpyW(rel_path, entries[i].name);
        }

        lstrcpyW(full_path, dir_path);
        if (!PathAppendW(full_path, entries[i].name)) {
            continue;
        }

        if (!append_item(t, rel_path, is_dir, &index)) {
            break;
        }

        if (t->hwnd) {
            insert.hParent = parent_item;
            insert.hInsertAfter = TVI_LAST;
            insert.item.mask = TVIF_TEXT | TVIF_PARAM;
            insert.item.pszText = entries[i].name;
            insert.item.lParam = (LPARAM)(uintptr_t)index;
            inserted = TreeView_InsertItem(t->hwnd, &insert);
        }

        if (is_dir) {
            walk_dir(t, full_path, rel_path, inserted, depth + 1);
        }
    }

    LocalFree(entries);
}

static LRESULT CALLBACK save_tree_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR subclass_id, DWORD_PTR ref_data) {
    save_tree_t *t = (save_tree_t *)ref_data;

    (void)subclass_id;

    switch (msg) {
    case WM_MOUSEMOVE:
        if (t && t->dragging && t->drag_image) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            TVHITTESTINFO hit = {0};

            ImageList_DragMove(pt.x, pt.y);

            hit.pt = pt;
            TreeView_HitTest(hwnd, &hit);
            TreeView_SelectDropTarget(hwnd, hit.hItem);
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (t && t->dragging) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            TVHITTESTINFO hit = {0};
            save_item_t src_item;
            wchar_t dst_parent_relpath[MAX_PATH];

            hit.pt = pt;
            TreeView_HitTest(hwnd, &hit);

            if (get_item_info(t, t->drag_src, NULL, &src_item)
                && choose_drop_parent(t, hit.hItem, dst_parent_relpath, MAX_PATH)) {
                save_tree_move(t, src_item.relative_path, dst_parent_relpath);
            }

            end_drag(t);
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        if (t && t->dragging && (HWND)lp != hwnd) {
            end_drag(t);
            return 0;
        }
        break;

    case WM_NCDESTROY:
        if (t) {
            end_drag(t);
        }
        RemoveWindowSubclass(hwnd, save_tree_subclass_proc, 1);
        break;
    }

    return DefSubclassProc(hwnd, msg, wp, lp);
}

save_tree_t *save_tree_create(HWND parent, HINSTANCE hinst, int id) {
    save_tree_t *t = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(save_tree_t));

    if (!t) {
        return NULL;
    }

    if (parent) {
        t->hwnd = CreateWindowExW(0, WC_TREEVIEWW,
            L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
            TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
            TVS_EDITLABELS | TVS_SHOWSELALWAYS,
            0, 0, 0, 0, parent, (HMENU)(uintptr_t)id, hinst, NULL);
        if (!t->hwnd) {
            LocalFree(t);
            return NULL;
        }
        SetWindowSubclass(t->hwnd, save_tree_subclass_proc, 1, (DWORD_PTR)t);
    }

    t->item_capacity = 64;
    t->items = LocalAlloc(LMEM_FIXED, t->item_capacity * sizeof(save_item_t));
    if (!t->items) {
        if (t->hwnd) {
            DestroyWindow(t->hwnd);
        }
        LocalFree(t);
        return NULL;
    }

    return t;
}

void save_tree_destroy(save_tree_t *t) {
    if (!t) {
        return;
    }

    end_drag(t);
    if (t->items) {
        LocalFree(t->items);
    }
    if (t->hwnd) {
        DestroyWindow(t->hwnd);
    }
    LocalFree(t);
}

bool save_tree_set_root(save_tree_t *t, const wchar_t *root_path) {
    if (!t || !root_path) {
        return false;
    }

    lstrcpynW(t->root_path, root_path, MAX_PATH);
    save_tree_refresh(t);
    return true;
}

void save_tree_refresh(save_tree_t *t) {
    const wchar_t *root_name;
    HTREEITEM wrapper_hitem = TVI_ROOT;
    size_t wrapper_index;

    if (!t) {
        return;
    }

    if (t->dragging) {
        end_drag(t);
    }

    if (t->hwnd) {
        TreeView_DeleteAllItems(t->hwnd);
    }
    t->item_count = 0;
    if (t->root_path[0] == L'\0') {
        return;
    }

    if (!append_item(t, L"", true, &wrapper_index)) {
        return;
    }

    root_name = PathFindFileNameW(t->root_path);
    if (!root_name || root_name == t->root_path || root_name[0] == L'\0') {
        root_name = t->root_path;
    }

    if (t->hwnd) {
        TVINSERTSTRUCTW insert = {0};

        insert.hParent = TVI_ROOT;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM;
        insert.item.pszText = (PWSTR)root_name;
        insert.item.lParam = (LPARAM)(uintptr_t)wrapper_index;
        wrapper_hitem = TreeView_InsertItem(t->hwnd, &insert);
    }

    walk_dir(t, t->root_path, L"", wrapper_hitem, 0);

    if (t->hwnd && wrapper_hitem) {
        TreeView_Expand(t->hwnd, wrapper_hitem, TVE_EXPAND);
    }
}

void save_tree_refresh_preserve_selection(save_tree_t *t) {
    wchar_t saved_relpath[MAX_PATH] = {0};
    wchar_t (*expanded_paths)[MAX_PATH] = NULL;
    size_t expanded_count = 0;
    wchar_t try_path[MAX_PATH];

    if (!t) {
        return;
    }

    if (t->dragging) {
        end_drag(t);
    }

    if (t->hwnd) {
        HTREEITEM selection = TreeView_GetSelection(t->hwnd);
        save_item_t item;

        if (selection && get_item_info(t, selection, NULL, &item)) {
            lstrcpynW(saved_relpath, item.relative_path, MAX_PATH);
        }

        expanded_paths = (wchar_t (*)[MAX_PATH])LocalAlloc(
            LMEM_FIXED, SAVE_TREE_MAX_EXPANDED_PATHS * sizeof(wchar_t[MAX_PATH]));
        if (expanded_paths) {
            HTREEITEM root = TreeView_GetRoot(t->hwnd);
            if (root) {
                collect_expanded_paths(t, root, expanded_paths,
                    SAVE_TREE_MAX_EXPANDED_PATHS, &expanded_count);
            }
        }
    }

    save_tree_refresh(t);

    if (!t->hwnd) {
        if (expanded_paths) {
            LocalFree(expanded_paths);
        }
        return;
    }

    if (expanded_paths) {
        HTREEITEM root = TreeView_GetRoot(t->hwnd);

        for (size_t i = 0; i < expanded_count; i++) {
            size_t index;
            HTREEITEM found;

            if (expanded_paths[i][0] == L'\0') {
                continue;
            }

            if (!find_item_index_by_relpath(t, expanded_paths[i], &index)) {
                continue;
            }

            found = find_hitem_by_lparam(t->hwnd, root, (LPARAM)(uintptr_t)index);
            if (found) {
                TreeView_Expand(t->hwnd, found, TVE_EXPAND);
            }
        }

        LocalFree(expanded_paths);
    }

    if (saved_relpath[0] == L'\0') {
        HTREEITEM root = TreeView_GetRoot(t->hwnd);
        if (root) {
            TreeView_SelectItem(t->hwnd, root);
        }
        return;
    }

    lstrcpynW(try_path, saved_relpath, MAX_PATH);
    while (true) {
        size_t index;

        if (find_item_index_by_relpath(t, try_path, &index)) {
            HTREEITEM found = find_hitem_by_lparam(t->hwnd, TreeView_GetRoot(t->hwnd), (LPARAM)(uintptr_t)index);
            if (found) {
                TreeView_SelectItem(t->hwnd, found);
            }
            return;
        }

        if (try_path[0] == L'\0') {
            break;
        }

        {
            wchar_t *last_sep = wcsrchr(try_path, L'\\');
            if (last_sep) {
                *last_sep = L'\0';
            } else {
                try_path[0] = L'\0';
            }
        }
    }

    {
        HTREEITEM root = TreeView_GetRoot(t->hwnd);
        if (root) {
            TreeView_SelectItem(t->hwnd, root);
        }
    }
}

bool save_tree_get_selected_path(const save_tree_t *t, wchar_t *out, size_t out_chars) {
    HTREEITEM selection;
    save_item_t item;

    if (!t || !t->hwnd || !out || out_chars == 0) {
        return false;
    }

    selection = TreeView_GetSelection(t->hwnd);
    if (!selection || !get_item_info(t, selection, NULL, &item)) {
        return false;
    }

    return build_full_path(t, item.relative_path, out, out_chars);
}

bool save_tree_get_selected_dir(const save_tree_t *t, wchar_t *out, size_t out_chars) {
    HTREEITEM selection;
    save_item_t item;

    if (!t || !out || out_chars == 0) {
        return false;
    }

    if (!t->hwnd) {
        if (t->root_path[0] == L'\0') {
            return false;
        }
        lstrcpynW(out, t->root_path, (int)out_chars);
        return true;
    }

    selection = TreeView_GetSelection(t->hwnd);
    if (!selection) {
        lstrcpynW(out, t->root_path, (int)out_chars);
        return true;
    }

    if (!get_item_info(t, selection, NULL, &item)) {
        return false;
    }

    if (item.relative_path[0] == L'\0') {
        lstrcpynW(out, t->root_path, (int)out_chars);
        return true;
    }

    if (item.is_directory) {
        return build_full_path(t, item.relative_path, out, out_chars);
    }

    if (!build_full_path(t, item.relative_path, out, out_chars)) {
        return false;
    }

    return PathRemoveFileSpecW(out) == TRUE;
}

HWND save_tree_get_hwnd(const save_tree_t *t) {
    return t ? t->hwnd : NULL;
}

bool save_tree_rename(save_tree_t *t, const wchar_t *old_relpath, const wchar_t *new_name) {
    wchar_t old_full[MAX_PATH];
    wchar_t parent_full[MAX_PATH];
    wchar_t new_full[MAX_PATH];
    const wchar_t *old_leaf;

    if (!t || !old_relpath || !new_name || !is_valid_name(new_name)) {
        return false;
    }

    if (!build_full_path(t, old_relpath, old_full, MAX_PATH)) {
        return false;
    }

    old_leaf = PathFindFileNameW(old_relpath);
    if (lstrcmpW(old_leaf, new_name) == 0) {
        return true;
    }

    lstrcpyW(parent_full, old_full);
    if (!PathRemoveFileSpecW(parent_full)) {
        return false;
    }

    lstrcpyW(new_full, parent_full);
    if (!PathAppendW(new_full, new_name)) {
        return false;
    }

    if (!MoveFileExW(old_full, new_full, MOVEFILE_REPLACE_EXISTING)) {
        return false;
    }

    save_tree_refresh(t);
    return true;
}

bool save_tree_delete(save_tree_t *t, const wchar_t *relpath) {
    wchar_t full[MAX_PATH + 2];
    SHFILEOPSTRUCTW op = {0};

    if (!t || !relpath || !build_full_path(t, relpath, full, MAX_PATH + 1)) {
        return false;
    }

    full[lstrlenW(full) + 1] = L'\0';

    op.wFunc = FO_DELETE;
    op.pFrom = full;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted) {
        return false;
    }

    save_tree_refresh(t);
    return true;
}

bool save_tree_new_folder(save_tree_t *t, const wchar_t *parent_relpath, const wchar_t *name) {
    wchar_t full[MAX_PATH];

    if (!t || !name || !is_valid_name(name) || !build_full_path(t, parent_relpath, full, MAX_PATH)) {
        return false;
    }

    if (!PathAppendW(full, name)) {
        return false;
    }

    if (!CreateDirectoryW(full, NULL)) {
        return false;
    }

    save_tree_refresh(t);
    return true;
}

bool save_tree_move(save_tree_t *t, const wchar_t *src_relpath, const wchar_t *dst_parent_relpath) {
    wchar_t src_full[MAX_PATH + 2];
    wchar_t dst_full[MAX_PATH + 2];
    wchar_t src_norm[MAX_PATH];
    wchar_t dst_norm[MAX_PATH];
    SHFILEOPSTRUCTW op = {0};

    if (!t || !src_relpath
        || !build_full_path(t, src_relpath, src_full, MAX_PATH + 1)
        || !build_full_path(t, dst_parent_relpath, dst_full, MAX_PATH + 1)) {
        return false;
    }

    lstrcpyW(src_norm, src_full);
    if (!PathAddBackslashW(src_norm)) {
        return false;
    }
    lstrcpyW(dst_norm, dst_full);
    if (!PathAddBackslashW(dst_norm)) {
        return false;
    }

    if (wcsncmp(dst_norm, src_norm, lstrlenW(src_norm)) == 0) {
        return false;
    }
    if (lstrcmpiW(src_full, dst_full) == 0) {
        return false;
    }

    src_full[lstrlenW(src_full) + 1] = L'\0';
    dst_full[lstrlenW(dst_full) + 1] = L'\0';

    op.wFunc = FO_MOVE;
    op.pFrom = src_full;
    op.pTo = dst_full;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted) {
        return false;
    }

    save_tree_refresh(t);
    return true;
}

int save_tree_item_count(const save_tree_t *t) {
    return t ? (int)t->item_count : 0;
}

bool save_tree_handle_notify(save_tree_t *t, LPNMHDR pnm, LRESULT *out_result) {
    if (!t || !pnm || pnm->hwndFrom != t->hwnd) {
        return false;
    }

    switch (pnm->code) {
    case TVN_BEGINLABELEDITW:
    case TVN_BEGINLABELEDITA:
        if (out_result) {
            *out_result = FALSE;
        }
        return true;

    case TVN_ENDLABELEDITW:
    case TVN_ENDLABELEDITA:
        {
            NMTVDISPINFOW *info = (NMTVDISPINFOW *)pnm;
            save_item_t item;
            bool ok = false;

            if (info->item.pszText && get_item_info(t, info->item.hItem, NULL, &item)) {
                ok = save_tree_rename(t, item.relative_path, info->item.pszText);
            }
            if (out_result) {
                *out_result = ok ? TRUE : FALSE;
            }
            return true;
        }

    case TVN_BEGINDRAGW:
    case TVN_BEGINDRAGA:
        {
            NMTREEVIEWW *info = (NMTREEVIEWW *)pnm;

            end_drag(t);
            t->dragging = true;
            t->drag_src = info->itemNew.hItem;
            t->drag_image = TreeView_CreateDragImage(t->hwnd, t->drag_src);
            if (t->drag_image) {
                ImageList_BeginDrag(t->drag_image, 0, 0, 0);
                ImageList_DragEnter(t->hwnd, info->ptDrag.x, info->ptDrag.y);
            }
            SetCapture(t->hwnd);
            if (out_result) {
                *out_result = 0;
            }
            return true;
        }

    case NM_RCLICK:
        {
            DWORD pos = GetMessagePos();
            POINT screen_pt = { GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };
            POINT client_pt = screen_pt;
            TVHITTESTINFO hit = {0};
            HMENU menu;
            UINT cmd;
            save_item_t item;
            wchar_t parent_relpath[MAX_PATH];

            ScreenToClient(t->hwnd, &client_pt);
            hit.pt = client_pt;
            TreeView_HitTest(t->hwnd, &hit);
            if (hit.hItem) {
                TreeView_SelectItem(t->hwnd, hit.hItem);
            }

            menu = CreatePopupMenu();
            if (!menu) {
                return true;
            }

            AppendMenuW(menu, MF_STRING, ID_SAVE_TREE_NEW_FOLDER, praxis_locale_str(STR_PRAXIS_NEW_FOLDER));
            AppendMenuW(menu, MF_STRING, ID_SAVE_TREE_RENAME, praxis_locale_str(STR_PRAXIS_RENAME));
            AppendMenuW(menu, MF_STRING, ID_SAVE_TREE_DELETE, praxis_locale_str(STR_PRAXIS_DELETE));

            cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0,
                GetParent(t->hwnd), NULL);
            DestroyMenu(menu);

            switch (cmd) {
            case ID_SAVE_TREE_NEW_FOLDER:
                if (hit.hItem && get_item_info(t, hit.hItem, NULL, &item)) {
                    if (item.is_directory) {
                        create_unique_folder(t, item.relative_path);
                    } else if (get_parent_relpath(item.relative_path, parent_relpath, MAX_PATH)) {
                        create_unique_folder(t, parent_relpath);
                    }
                } else {
                    create_unique_folder(t, L"");
                }
                break;

            case ID_SAVE_TREE_RENAME:
                if (hit.hItem) {
                    TreeView_EditLabel(t->hwnd, hit.hItem);
                }
                break;

            case ID_SAVE_TREE_DELETE:
                if (hit.hItem && get_item_info(t, hit.hItem, NULL, &item)) {
                    save_tree_delete(t, item.relative_path);
                }
                break;
            }

            if (out_result) {
                *out_result = 0;
            }
            return true;
        }
    }

    return false;
}
