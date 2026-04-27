/**
 * @file save_tree.c
 * @brief TreeView-backed save library widget for Praxis.
 * @details Enumerates a rooted filesystem tree, exposes headless file operations,
 *          and handles TreeView rename, context menu, and drag-move behavior.
 */

#include "save_tree_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlwapi.h>

#define SAVE_TREE_MAX_DEPTH 16
#define SAVE_TREE_MAX_EXPANDED_PATHS 256

/* Forward declaration: refresh body without redraw bracketing.
 * Public save_tree_refresh() / save_tree_refresh_preserve_selection() handle
 * the WM_SETREDRAW envelope themselves to suppress flicker across the entire
 * rebuild + reselect + reexpand sequence. */
static void save_tree_refresh_inner(save_tree_t *t);

HIMAGELIST save_tree_get_system_image_list(void) {
    SHFILEINFOW sfi;

    ZeroMemory(&sfi, sizeof(sfi));
    return (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi),
        SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
}

int save_tree_resolve_icon_index(const wchar_t *name, bool is_directory) {
    SHFILEINFOW sfi;
    DWORD attrs = is_directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    DWORD flags = SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES | SHGFI_SMALLICON;
    const wchar_t *probe = (name && name[0] != L'\0') ? name : (is_directory ? L"folder" : L"file");

    ZeroMemory(&sfi, sizeof(sfi));
    if (!SHGetFileInfoW(probe, attrs, &sfi, sizeof(sfi), flags)) {
        return -1;
    }
    return sfi.iIcon;
}

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

bool save_tree_build_full_path(const save_tree_t *t, const wchar_t *relpath, wchar_t *out, size_t out_chars) {
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

bool save_tree_get_item_info(const save_tree_t *t, HTREEITEM item, size_t *out_index, save_item_t *out_value) {
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

bool save_tree_get_parent_relpath(const wchar_t *relpath, wchar_t *out, size_t out_chars) {
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

bool save_tree_append_item(save_tree_t *t, const wchar_t *relative_path, bool is_directory,
    bool is_readonly, const FILETIME *last_write_time, size_t *out_index) {
    size_t index;

    if (!t || !relative_path || !ensure_capacity(t)) {
        return false;
    }

    index = t->item_count;
    lstrcpynW(t->items[index].relative_path, relative_path, MAX_PATH);
    t->items[index].is_directory = is_directory;
    t->items[index].is_readonly = !is_directory && is_readonly;
    if (last_write_time) {
        t->items[index].last_write_time = *last_write_time;
    } else {
        ZeroMemory(&t->items[index].last_write_time, sizeof(t->items[index].last_write_time));
    }
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

save_tree_t *save_tree_create(HWND parent, HINSTANCE hinst, int id) {
    save_tree_t *t = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(save_tree_t));

    if (!t) {
        return NULL;
    }

    t->folder_icon_idx = -1;
    t->sort_mode = SAVE_TREE_SORT_NAME_ASC;

    if (parent) {
        HIMAGELIST sys_imgl;

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

        /* Borrow the shell's small system image list so files/folders show
         * their natural icons. The shell owns the image list — we never
         * destroy it. */
        sys_imgl = save_tree_get_system_image_list();
        if (sys_imgl) {
            TreeView_SetImageList(t->hwnd, sys_imgl, TVSIL_NORMAL);
        }
        t->folder_icon_idx = save_tree_resolve_icon_index(L"folder", true);
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

    save_tree_end_drag(t);
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

static bool save_tree_sort_mode_is_valid(save_tree_sort_mode_t mode) {
    return mode >= SAVE_TREE_SORT_NAME_ASC && mode <= SAVE_TREE_SORT_MODIFIED_DESC;
}

void save_tree_set_sort_mode(save_tree_t *t, save_tree_sort_mode_t mode) {
    if (!t) {
        return;
    }
    if (!save_tree_sort_mode_is_valid(mode)) {
        mode = SAVE_TREE_SORT_NAME_ASC;
    }
    if (t->sort_mode == mode) {
        return;
    }

    t->sort_mode = mode;
    if (t->root_path[0] != L'\0') {
        save_tree_refresh_preserve_selection(t);
    }
}

save_tree_sort_mode_t save_tree_get_sort_mode(const save_tree_t *t) {
    return t ? t->sort_mode : SAVE_TREE_SORT_NAME_ASC;
}

/* Inner refresh: rebuild items[] and TreeView nodes WITHOUT touching the
 * WM_SETREDRAW state. Public refresh wrappers handle the redraw envelope. */
static void save_tree_refresh_inner(save_tree_t *t) {
    const wchar_t *root_name;
    HTREEITEM wrapper_hitem = TVI_ROOT;
    size_t wrapper_index;

    if (!t) {
        return;
    }

    if (t->dragging) {
        save_tree_end_drag(t);
    }

    if (t->hwnd) {
        TreeView_DeleteAllItems(t->hwnd);
    }
    t->item_count = 0;
    if (t->root_path[0] == L'\0') {
        return;
    }

    if (!save_tree_append_item(t, L"", true, false, NULL, &wrapper_index)) {
        return;
    }

    root_name = PathFindFileNameW(t->root_path);
    if (!root_name || root_name == t->root_path || root_name[0] == L'\0') {
        root_name = t->root_path;
    }

    if (t->hwnd) {
        TVINSERTSTRUCTW insert = {0};
        int folder_icon = (t->folder_icon_idx >= 0)
            ? t->folder_icon_idx
            : save_tree_resolve_icon_index(L"folder", true);

        insert.hParent = TVI_ROOT;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        insert.item.pszText = (PWSTR)root_name;
        insert.item.lParam = (LPARAM)(uintptr_t)wrapper_index;
        insert.item.iImage = folder_icon;
        insert.item.iSelectedImage = folder_icon;
        wrapper_hitem = TreeView_InsertItem(t->hwnd, &insert);
    }

    save_tree_walk_dir(t, t->root_path, L"", wrapper_hitem, 0);

    if (t->hwnd && wrapper_hitem) {
        TreeView_Expand(t->hwnd, wrapper_hitem, TVE_EXPAND);
    }
}

void save_tree_refresh(save_tree_t *t) {
    if (!t) {
        return;
    }

    /* Anti-flicker: suppress redraw while the items[] array and TreeView
     * nodes are torn down and rebuilt. Re-enable redraw and force a single
     * invalidate at the end so the user sees only the final, complete state. */
    if (t->hwnd) {
        SendMessageW(t->hwnd, WM_SETREDRAW, FALSE, 0);
    }

    save_tree_refresh_inner(t);

    if (t->hwnd) {
        SendMessageW(t->hwnd, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(t->hwnd, NULL, NULL,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
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
        save_tree_end_drag(t);
    }

    /* Anti-flicker: bracket the entire capture/refresh/restore sequence in
     * a single WM_SETREDRAW=FALSE..TRUE window so the user only sees the
     * final state. */
    if (t->hwnd) {
        SendMessageW(t->hwnd, WM_SETREDRAW, FALSE, 0);

        HTREEITEM selection = TreeView_GetSelection(t->hwnd);
        save_item_t item;

        if (selection && save_tree_get_item_info(t, selection, NULL, &item)) {
            lstrcpynW(saved_relpath, item.relative_path, MAX_PATH);
        }

        expanded_paths = (wchar_t (*)[MAX_PATH])LocalAlloc(
            LMEM_FIXED, SAVE_TREE_MAX_EXPANDED_PATHS * sizeof(wchar_t[MAX_PATH]));
        if (expanded_paths) {
            HTREEITEM root = TreeView_GetRoot(t->hwnd);
            if (root) {
                save_tree_collect_expanded_paths(t, root, expanded_paths,
                    SAVE_TREE_MAX_EXPANDED_PATHS, &expanded_count);
            }
        }
    }

    save_tree_refresh_inner(t);

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
        SendMessageW(t->hwnd, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(t->hwnd, NULL, NULL,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
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
            SendMessageW(t->hwnd, WM_SETREDRAW, TRUE, 0);
            RedrawWindow(t->hwnd, NULL, NULL,
                RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
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

    SendMessageW(t->hwnd, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(t->hwnd, NULL, NULL,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

bool save_tree_get_selected_path(const save_tree_t *t, wchar_t *out, size_t out_chars) {
    HTREEITEM selection;
    save_item_t item;

    if (!t || !t->hwnd || !out || out_chars == 0) {
        return false;
    }

    selection = TreeView_GetSelection(t->hwnd);
    if (!selection || !save_tree_get_item_info(t, selection, NULL, &item)) {
        return false;
    }

    return save_tree_build_full_path(t, item.relative_path, out, out_chars);
}

bool save_tree_select_full_path(save_tree_t *t, const wchar_t *full_path) {
    wchar_t relpath[MAX_PATH];
    size_t root_len;
    size_t index;
    HTREEITEM htarget;
    const wchar_t *suffix;
    wchar_t *p;

    if (!t || !t->hwnd || !full_path) {
        return false;
    }

    root_len = (size_t)lstrlenW(t->root_path);
    if (root_len == 0) {
        return false;
    }

    if (CompareStringOrdinal(full_path, (int)root_len, t->root_path, (int)root_len, TRUE) != CSTR_EQUAL) {
        return false;
    }

    if (full_path[root_len] != L'\0' && full_path[root_len] != L'\\' && full_path[root_len] != L'/') {
        return false;
    }

    suffix = full_path + root_len;
    while (*suffix == L'\\' || *suffix == L'/') {
        suffix++;
    }
    lstrcpynW(relpath, suffix, MAX_PATH);

    for (p = relpath; *p; p++) {
        if (*p == L'/') {
            *p = L'\\';
        }
    }

    if (!find_item_index_by_relpath(t, relpath, &index)) {
        return false;
    }

    htarget = find_hitem_by_lparam(t->hwnd, TreeView_GetRoot(t->hwnd), (LPARAM)(uintptr_t)index);
    if (!htarget) {
        return false;
    }

    TreeView_SelectItem(t->hwnd, htarget);
    TreeView_EnsureVisible(t->hwnd, htarget);
    return true;
}

bool save_tree_select_sibling_file(save_tree_t *t, int direction) {
    HTREEITEM selection;
    save_item_t selected_item;
    size_t selected_index = (size_t)-1;
    wchar_t parent_relpath[MAX_PATH];
    size_t first_candidate = (size_t)-1;
    size_t last_candidate = (size_t)-1;
    size_t prev_candidate = (size_t)-1;
    size_t target_index = (size_t)-1;
    bool selected_is_file = false;
    bool selected_seen = false;
    int step = direction < 0 ? -1 : 1;

    if (!t || !t->hwnd || direction == 0) {
        return false;
    }

    parent_relpath[0] = L'\0';
    selection = TreeView_GetSelection(t->hwnd);
    if (selection && save_tree_get_item_info(t, selection, &selected_index, &selected_item)) {
        if (selected_item.is_directory) {
            lstrcpynW(parent_relpath, selected_item.relative_path, MAX_PATH);
        } else {
            selected_is_file = save_tree_get_parent_relpath(selected_item.relative_path,
                parent_relpath, MAX_PATH);
        }
    }

    for (size_t i = 0; i < t->item_count; i++) {
        wchar_t item_parent[MAX_PATH];

        if (t->items[i].is_directory) {
            continue;
        }
        if (!save_tree_get_parent_relpath(t->items[i].relative_path, item_parent, MAX_PATH) ||
            lstrcmpW(item_parent, parent_relpath) != 0) {
            continue;
        }

        if (first_candidate == (size_t)-1) {
            first_candidate = i;
        }

        if (selected_is_file && i == selected_index) {
            selected_seen = true;
            if (step < 0) {
                target_index = prev_candidate;
                if (target_index != (size_t)-1) {
                    break;
                }
            }
        } else if (selected_seen && step > 0) {
            target_index = i;
            break;
        }

        prev_candidate = i;
        last_candidate = i;
    }

    if (first_candidate == (size_t)-1) {
        return false;
    }
    if (target_index == (size_t)-1) {
        target_index = step > 0 ? first_candidate : last_candidate;
    }

    {
        HTREEITEM target = find_hitem_by_lparam(t->hwnd, TreeView_GetRoot(t->hwnd),
            (LPARAM)(uintptr_t)target_index);
        if (!target) {
            return false;
        }

        TreeView_SelectItem(t->hwnd, target);
        TreeView_EnsureVisible(t->hwnd, target);
    }

    return true;
}

bool save_tree_get_selected_file_readonly(const save_tree_t *t, bool *out_readonly) {
    HTREEITEM selection;
    save_item_t item;

    if (out_readonly) {
        *out_readonly = false;
    }
    if (!t || !t->hwnd || !out_readonly) {
        return false;
    }

    selection = TreeView_GetSelection(t->hwnd);
    if (!selection || !save_tree_get_item_info(t, selection, NULL, &item) || item.is_directory) {
        return false;
    }

    *out_readonly = item.is_readonly;
    return true;
}

bool save_tree_selected_file_can_replace(const save_tree_t *t) {
    bool is_readonly = false;

    return save_tree_get_selected_file_readonly(t, &is_readonly) && !is_readonly;
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

    if (!save_tree_get_item_info(t, selection, NULL, &item)) {
        return false;
    }

    if (item.relative_path[0] == L'\0') {
        lstrcpynW(out, t->root_path, (int)out_chars);
        return true;
    }

    if (item.is_directory) {
        return save_tree_build_full_path(t, item.relative_path, out, out_chars);
    }

    if (!save_tree_build_full_path(t, item.relative_path, out, out_chars)) {
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
    wchar_t parent_relpath[MAX_PATH];
    wchar_t new_relpath[MAX_PATH];
    const wchar_t *old_leaf;

    if (!t || !old_relpath || !new_name || !is_valid_name(new_name)) {
        return false;
    }

    if (!save_tree_build_full_path(t, old_relpath, old_full, MAX_PATH)) {
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

    /* Compute the new relative path so we can re-select the renamed node
     * after the tree is rebuilt. The file watcher debounce will see this
     * selection and preserve it via the exact-match walk-up path. */
    new_relpath[0] = L'\0';
    if (save_tree_get_parent_relpath(old_relpath, parent_relpath, MAX_PATH)) {
        if (parent_relpath[0] != L'\0') {
            lstrcpynW(new_relpath, parent_relpath, MAX_PATH);
            if (!PathAppendW(new_relpath, new_name)) {
                new_relpath[0] = L'\0';
            }
        } else {
            lstrcpynW(new_relpath, new_name, MAX_PATH);
        }
    }

    save_tree_refresh(t);

    /* Re-select the renamed node. Without this the previous selection is
     * lost and the watcher's later refresh_preserve_selection falls back
     * to the parent (or root) via walk-up. */
    if (t->hwnd && new_relpath[0] != L'\0') {
        size_t idx;
        if (find_item_index_by_relpath(t, new_relpath, &idx)) {
            HTREEITEM hitem = find_hitem_by_lparam(t->hwnd,
                TreeView_GetRoot(t->hwnd), (LPARAM)(uintptr_t)idx);
            if (hitem) {
                TreeView_SelectItem(t->hwnd, hitem);
                TreeView_EnsureVisible(t->hwnd, hitem);
            }
        }
    }

    return true;
}

bool save_tree_delete(save_tree_t *t, const wchar_t *relpath) {
    wchar_t full[MAX_PATH + 2];
    SHFILEOPSTRUCTW op = {0};

    if (!t || !relpath || !save_tree_build_full_path(t, relpath, full, MAX_PATH + 1)) {
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

    if (!t || !name || !is_valid_name(name) || !save_tree_build_full_path(t, parent_relpath, full, MAX_PATH)) {
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
        || !save_tree_build_full_path(t, src_relpath, src_full, MAX_PATH + 1)
        || !save_tree_build_full_path(t, dst_parent_relpath, dst_full, MAX_PATH + 1)) {
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

bool save_tree_set_file_readonly(save_tree_t *t, const wchar_t *relpath, bool read_only) {
    wchar_t full[MAX_PATH];
    DWORD attrs;
    DWORD new_attrs;

    if (!t || !relpath || relpath[0] == L'\0' ||
        !save_tree_build_full_path(t, relpath, full, MAX_PATH)) {
        return false;
    }

    attrs = GetFileAttributesW(full);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }

    new_attrs = read_only ? (attrs | FILE_ATTRIBUTE_READONLY) : (attrs & ~FILE_ATTRIBUTE_READONLY);
    if (new_attrs == attrs) {
        return true;
    }
    if (!SetFileAttributesW(full, new_attrs)) {
        return false;
    }

    save_tree_refresh_preserve_selection(t);
    return true;
}

int save_tree_item_count(const save_tree_t *t) {
    return t ? (int)t->item_count : 0;
}

