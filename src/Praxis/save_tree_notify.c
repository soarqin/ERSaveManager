/**
 * @file save_tree_notify.c
 * @brief WM_NOTIFY handling for the Praxis save tree.
 */

#include "save_tree_notify.h"

#include "locale.h"
#include "../common/theme_core.h"

#include "save_tree_internal.h"

#include <stdbool.h>
#include <stddef.h>

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#define ID_SAVE_TREE_NEW_FOLDER       50001
#define ID_SAVE_TREE_RENAME           50002
#define ID_SAVE_TREE_DELETE           50003
#define ID_SAVE_TREE_SHOW_IN_EXPLORER 50004
#define ID_SAVE_TREE_SET_READONLY     50005

/* Show a Yes/No confirmation dialog before deleting a save tree item.
 * The parent is the main window (tree's parent) so the dialog is centered
 * on the application rather than on the tree control. */
static bool confirm_save_tree_delete(const save_tree_t *t) {
    HWND parent = (t && t->hwnd) ? GetParent(t->hwnd) : NULL;
    return MessageBoxW(parent,
        praxis_locale_str(STR_PRAXIS_CONFIRM_DELETE_SAVE),
        praxis_locale_str(STR_PRAXIS_CONFIRM),
        MB_YESNO | MB_ICONQUESTION) == IDYES;
}

/* Compute the visual display name for a tree item.
 *  - For directories: identical to leaf name.
 *  - For files: leaf name with extension stripped. We display ext-less names
 *    for a friendlier UX; the actual filename on disk keeps its extension and
 *    is used for all I/O via items[i].relative_path. */
void save_tree_make_display_name(const wchar_t *leaf, bool is_directory,
    bool is_readonly, wchar_t *out, size_t out_chars) {
    if (!out || out_chars == 0) {
        return;
    }
    if (!leaf) {
        out[0] = L'\0';
        return;
    }
    lstrcpynW(out, leaf, (int)out_chars);
    if (!is_directory) {
        PathRemoveExtensionW(out);
        if (is_readonly) {
            const wchar_t *marker = praxis_locale_str(STR_PRAXIS_READ_ONLY_MARK);
            size_t cur_len = (size_t)lstrlenW(out);
            size_t marker_len = marker ? (size_t)lstrlenW(marker) : 0;

            if (marker && cur_len + marker_len + 1 <= out_chars) {
                lstrcatW(out, marker);
            }
        }
    }
}

void save_tree_end_drag(save_tree_t *t) {
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

/* Open the user's file manager at @p full_path and highlight the item.
 *  - For files / non-root directories, uses SHOpenFolderAndSelectItems to
 *    open the parent folder with the target highlighted (Windows Explorer
 *    "Reveal in Folder" behavior).
 *  - For the tree root itself (selecting it has no logical parent within
 *    Praxis), simply opens the folder via ShellExecute. The caller decides
 *    which mode by passing @p select_in_parent.
 * Returns true on apparent success. Failures are silent (best-effort UX). */
static bool save_tree_reveal_in_explorer(const wchar_t *full_path, bool select_in_parent) {
    if (!full_path || full_path[0] == L'\0') {
        return false;
    }

    if (!select_in_parent) {
        HINSTANCE rv = ShellExecuteW(NULL, L"open", full_path, NULL, NULL, SW_SHOWNORMAL);
        return (INT_PTR)rv > 32;
    }

    PIDLIST_ABSOLUTE pidl = NULL;
    HRESULT hr = SHParseDisplayName(full_path, NULL, &pidl, 0, NULL);
    if (FAILED(hr) || !pidl) {
        wchar_t args[MAX_PATH + 16];
        HINSTANCE rv;

        _snwprintf_s(args, MAX_PATH + 16, _TRUNCATE, L"/select,\"%ls\"", full_path);
        rv = ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
        return (INT_PTR)rv > 32;
    }

    hr = SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
    CoTaskMemFree((void *)pidl);
    return SUCCEEDED(hr);
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
        _snwprintf_s(name, MAX_PATH, _TRUNCATE, L"%ls (%d)", base_name, suffix);
        if (save_tree_new_folder(t, parent_relpath, name)) {
            return true;
        }
    }

    return false;
}

static bool choose_drop_parent(const save_tree_t *t, HTREEITEM target,
    wchar_t *dst_parent_relpath, size_t out_chars) {
    save_item_t sel_item;

    if (!dst_parent_relpath || out_chars == 0) {
        return false;
    }

    dst_parent_relpath[0] = L'\0';
    if (!target) {
        return true;
    }

    if (!save_tree_get_item_info(t, target, NULL, &sel_item)) {
        return false;
    }

    if (sel_item.is_directory) {
        if ((size_t)lstrlenW(sel_item.relative_path) >= out_chars) {
            return false;
        }
        lstrcpyW(dst_parent_relpath, sel_item.relative_path);
        return true;
    }

    return save_tree_get_parent_relpath(sel_item.relative_path, dst_parent_relpath, out_chars);
}

LRESULT CALLBACK save_tree_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
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

            if (save_tree_get_item_info(t, t->drag_src, NULL, &src_item)
                && choose_drop_parent(t, hit.hItem, dst_parent_relpath, MAX_PATH)) {
                save_tree_move(t, src_item.relative_path, dst_parent_relpath);
            }

            save_tree_end_drag(t);
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        if (t && t->dragging && (HWND)lp != hwnd) {
            save_tree_end_drag(t);
            return 0;
        }
        break;

    case WM_NCDESTROY:
        if (t) {
            save_tree_end_drag(t);
        }
        RemoveWindowSubclass(hwnd, save_tree_subclass_proc, 1);
        break;
    }

    return DefSubclassProc(hwnd, msg, wp, lp);
}

bool save_tree_notify_handle(save_tree_t *t, NMHDR *nmhdr, LRESULT *result) {
    if (!t || !nmhdr || nmhdr->hwndFrom != t->hwnd) {
        return false;
    }

    switch (nmhdr->code) {
    case NM_CUSTOMDRAW:
        if (result) {
            *result = theme_core_on_treeview_customdraw((LPNMTVCUSTOMDRAW)nmhdr);
        }
        return true;

    case TVN_BEGINLABELEDITW:
    case TVN_BEGINLABELEDITA:
        {
            NMTVDISPINFOW *tv_disp_info = (NMTVDISPINFOW *)nmhdr;
            save_item_t sel_item;

            if (!tv_disp_info->item.hItem || !save_tree_get_item_info(t, tv_disp_info->item.hItem, NULL, &sel_item)
                || sel_item.relative_path[0] == L'\0') {
                if (result) {
                    *result = TRUE;
                }
                return true;
            }

            if (!sel_item.is_directory) {
                HWND edit = TreeView_GetEditControl(t->hwnd);
                if (edit) {
                    wchar_t base[MAX_PATH];
                    const wchar_t *leaf = PathFindFileNameW(sel_item.relative_path);

                    save_tree_make_display_name(leaf, false, false, base, MAX_PATH);
                    SetWindowTextW(edit, base);
                    SendMessageW(edit, EM_SETSEL, 0, (LPARAM)-1);
                }
            }

            if (result) {
                *result = FALSE;
            }
            return true;
        }

    case TVN_ENDLABELEDITW:
    case TVN_ENDLABELEDITA:
        {
            NMTVDISPINFOW *tv_disp_info = (NMTVDISPINFOW *)nmhdr;
            save_item_t sel_item;

            if (tv_disp_info->item.pszText && save_tree_get_item_info(t, tv_disp_info->item.hItem, NULL, &sel_item)) {
                wchar_t new_name[MAX_PATH];

                if (sel_item.is_directory) {
                    lstrcpynW(new_name, tv_disp_info->item.pszText, MAX_PATH);
                } else {
                    const wchar_t *old_leaf = PathFindFileNameW(sel_item.relative_path);
                    const wchar_t *old_ext = old_leaf ? PathFindExtensionW(old_leaf) : L"";
                    const wchar_t *user_ext;

                    lstrcpynW(new_name, tv_disp_info->item.pszText, MAX_PATH);
                    user_ext = PathFindExtensionW(new_name);
                    if (old_ext && old_ext[0] != L'\0'
                        && lstrcmpiW(user_ext, old_ext) != 0) {
                        size_t cur_len = (size_t)lstrlenW(new_name);
                        size_t ext_len = (size_t)lstrlenW(old_ext);
                        if (cur_len + ext_len + 1 <= MAX_PATH) {
                            lstrcatW(new_name, old_ext);
                        }
                    }
                }

                save_tree_rename(t, sel_item.relative_path, new_name);
            }

            if (result) {
                *result = FALSE;
            }
            return true;
        }

    case TVN_KEYDOWN:
        {
            NMTVKEYDOWN *kd = (NMTVKEYDOWN *)nmhdr;
            HTREEITEM sel = TreeView_GetSelection(t->hwnd);
            save_item_t sel_item;
            bool have_item = sel && save_tree_get_item_info(t, sel, NULL, &sel_item);

            switch (kd->wVKey) {
            case VK_F2:
                if (have_item && sel_item.relative_path[0] != L'\0') {
                    TreeView_EditLabel(t->hwnd, sel);
                }
                break;
            case VK_DELETE:
                if (have_item && sel_item.relative_path[0] != L'\0'
                    && confirm_save_tree_delete(t)) {
                    save_tree_delete(t, sel_item.relative_path);
                }
                break;
            }

            if (result) {
                *result = 0;
            }
            return true;
        }

    case TVN_BEGINDRAGW:
    case TVN_BEGINDRAGA:
        {
            NMTREEVIEWW *tv_nmtree = (NMTREEVIEWW *)nmhdr;

            save_tree_end_drag(t);
            t->dragging = true;
            t->drag_src = tv_nmtree->itemNew.hItem;
            t->drag_image = TreeView_CreateDragImage(t->hwnd, t->drag_src);
            if (t->drag_image) {
                ImageList_BeginDrag(t->drag_image, 0, 0, 0);
                ImageList_DragEnter(t->hwnd, tv_nmtree->ptDrag.x, tv_nmtree->ptDrag.y);
            }
            SetCapture(t->hwnd);
            if (result) {
                *result = 0;
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
            save_item_t sel_item;
            wchar_t parent_relpath[MAX_PATH];
            bool hit_is_file = false;

            ScreenToClient(t->hwnd, &client_pt);
            hit.pt = client_pt;
            TreeView_HitTest(t->hwnd, &hit);
            if (hit.hItem) {
                TreeView_SelectItem(t->hwnd, hit.hItem);
                if (save_tree_get_item_info(t, hit.hItem, NULL, &sel_item)) {
                    hit_is_file = sel_item.relative_path[0] != L'\0' && !sel_item.is_directory;
                }
            }

            menu = CreatePopupMenu();
            if (!menu) {
                return true;
            }

            AppendMenuW(menu, MF_STRING, ID_SAVE_TREE_NEW_FOLDER, praxis_locale_str(STR_PRAXIS_NEW_FOLDER));
            AppendMenuW(menu, MF_STRING, ID_SAVE_TREE_RENAME, praxis_locale_str(STR_PRAXIS_RENAME));
            AppendMenuW(menu, MF_STRING, ID_SAVE_TREE_DELETE, praxis_locale_str(STR_PRAXIS_DELETE));
            AppendMenuW(menu, hit_is_file ? MF_STRING : (MF_STRING | MF_GRAYED),
                ID_SAVE_TREE_SET_READONLY,
                praxis_locale_str(hit_is_file && sel_item.is_readonly
                    ? STR_PRAXIS_MAKE_WRITABLE
                    : STR_PRAXIS_MAKE_READ_ONLY));
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, ID_SAVE_TREE_SHOW_IN_EXPLORER,
                praxis_locale_str(STR_PRAXIS_SHOW_IN_EXPLORER));

            cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0,
                GetParent(t->hwnd), NULL);
            DestroyMenu(menu);

            switch (cmd) {
            case ID_SAVE_TREE_NEW_FOLDER:
                if (hit.hItem && save_tree_get_item_info(t, hit.hItem, NULL, &sel_item)) {
                    if (sel_item.is_directory) {
                        create_unique_folder(t, sel_item.relative_path);
                    } else if (save_tree_get_parent_relpath(sel_item.relative_path, parent_relpath, MAX_PATH)) {
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
                if (hit.hItem && save_tree_get_item_info(t, hit.hItem, NULL, &sel_item)
                    && sel_item.relative_path[0] != L'\0'
                    && confirm_save_tree_delete(t)) {
                    save_tree_delete(t, sel_item.relative_path);
                }
                break;

            case ID_SAVE_TREE_SET_READONLY:
                if (hit.hItem && save_tree_get_item_info(t, hit.hItem, NULL, &sel_item)
                    && sel_item.relative_path[0] != L'\0'
                    && !sel_item.is_directory) {
                    save_tree_set_file_readonly(t, sel_item.relative_path, !sel_item.is_readonly);
                }
                break;

            case ID_SAVE_TREE_SHOW_IN_EXPLORER:
                {
                    wchar_t full[MAX_PATH];
                    bool have_item = hit.hItem
                        && save_tree_get_item_info(t, hit.hItem, NULL, &sel_item)
                        && sel_item.relative_path[0] != L'\0';

                    if (have_item && save_tree_build_full_path(t, sel_item.relative_path, full, MAX_PATH)) {
                        save_tree_reveal_in_explorer(full, true);
                    } else if (t->root_path[0] != L'\0') {
                        save_tree_reveal_in_explorer(t->root_path, false);
                    }
                }
                break;
            }

            if (result) {
                *result = 0;
            }
            return true;
        }
    }

    return false;
}

bool save_tree_handle_notify(save_tree_t *t, LPNMHDR pnm, LRESULT *out_result) {
    return save_tree_notify_handle(t, pnm, out_result);
}
