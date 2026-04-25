/**
 * @file game_profile_manager.c
 * @brief Implementation of the Game Profile Manager modal dialog.
 * @details Provides ListView display of game profiles plus Add/Edit/Delete/Close buttons.
 *          Sort, filter, and drag-reorder are intentionally NOT supported (Phase 2 guardrail).
 */

#include "game_profile_manager.h"
#include "edit_game_profile.h"

#include "../locale.h"
#include "../profile_store.h"
#include "../resource.h"

#include <commctrl.h>
#include <stdbool.h>
#include <wchar.h>
#include <windows.h>

/* Dialog state pointer stored in DWLP_USER. */
typedef struct gpm_state_s {
    profile_store_t *store;
    const wchar_t *ini_path;
} gpm_state_t;

/* Look up the display name for a game_id_t. */
static const wchar_t *game_id_display_name(game_id_t gid) {
    switch (gid) {
    case GAME_ID_ELDEN_RING: return L"Elden Ring";
    default:                 return L"Unknown";
    }
}

/* Initialize ListView columns once on dialog creation. */
static void gpm_init_columns(HWND list) {
    LVCOLUMNW col;

    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.pszText = (LPWSTR)praxis_locale_str(STR_PRAXIS_PROFILE_NAME);
    col.cx = 100;
    col.iSubItem = 0;
    ListView_InsertColumn(list, 0, &col);

    col.pszText = (LPWSTR)praxis_locale_str(STR_PRAXIS_PROFILE_GAME);
    col.cx = 80;
    col.iSubItem = 1;
    ListView_InsertColumn(list, 1, &col);

    col.pszText = (LPWSTR)praxis_locale_str(STR_PRAXIS_PROFILE_SAVE_DIR);
    col.cx = 80;
    col.iSubItem = 2;
    ListView_InsertColumn(list, 2, &col);

    col.pszText = (LPWSTR)praxis_locale_str(STR_PRAXIS_PROFILE_TREE_ROOT);
    col.cx = 80;
    col.iSubItem = 3;
    ListView_InsertColumn(list, 3, &col);
}

/* Repopulate the ListView from the current store contents. */
static void gpm_refresh_list(HWND list, const profile_store_t *store) {
    ListView_DeleteAllItems(list);

    for (size_t i = 0; i < store->game_count; i++) {
        const game_profile_t *gp = &store->games[i];
        LVITEMW item;

        ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = (int)i;
        item.iSubItem = 0;
        item.pszText = (LPWSTR)gp->name;
        item.lParam = (LPARAM)gp->id;
        int idx = ListView_InsertItem(list, &item);
        if (idx < 0) {
            continue;
        }

        ListView_SetItemText(list, idx, 1, (LPWSTR)game_id_display_name(gp->game_id));
        ListView_SetItemText(list, idx, 2, (LPWSTR)gp->original_save_dir);
        ListView_SetItemText(list, idx, 3, (LPWSTR)gp->tree_root);
    }
}

/* Get the game_profile_t.id stored in the selected ListView row, or 0 if none. */
static int gpm_selected_game_id(HWND list) {
    int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (sel < 0) {
        return 0;
    }

    LVITEMW item;
    ZeroMemory(&item, sizeof(item));
    item.mask = LVIF_PARAM;
    item.iItem = sel;
    if (!ListView_GetItem(list, &item)) {
        return 0;
    }
    return (int)item.lParam;
}

/* Find a game profile by ID. Returns NULL if not found. */
static game_profile_t *gpm_find_game_by_id(profile_store_t *store, int id) {
    for (size_t i = 0; i < store->game_count; i++) {
        if (store->games[i].id == id) {
            return &store->games[i];
        }
    }
    return NULL;
}

/* Count backup profiles whose parent_game_id matches the given id. */
static size_t gpm_count_children(const profile_store_t *store, int game_id) {
    size_t n = 0;
    for (size_t i = 0; i < store->backup_count; i++) {
        if (store->backups[i].parent_game_id == game_id) {
            n++;
        }
    }
    return n;
}

/* Persist the store to disk via profile_store_save. */
static void gpm_persist(const profile_store_t *store, const wchar_t *ini_path) {
    if (ini_path && ini_path[0] != L'\0') {
        profile_store_save(store, ini_path);
    }
}

/* Handle the Add button: open dialog_edit_game_profile_show in create mode. */
static void gpm_handle_add(HWND hwnd, gpm_state_t *state) {
    game_profile_t gp;

    ZeroMemory(&gp, sizeof(gp));
    gp.game_id = GAME_ID_ELDEN_RING;

    if (dialog_edit_game_profile_show(hwnd, &gp, true) == IDOK) {
        if (profile_store_add_game(state->store, &gp) > 0) {
            gpm_persist(state->store, state->ini_path);
            gpm_refresh_list(GetDlgItem(hwnd, IDC_GPM_LIST), state->store);
        } else {
            MessageBoxW(hwnd, praxis_locale_str(STR_PRAXIS_ERROR),
                praxis_locale_str(STR_PRAXIS_ERROR), MB_OK | MB_ICONERROR);
        }
    }
}

/* Handle the Edit button: open dialog_edit_game_profile_show in edit mode. */
static void gpm_handle_edit(HWND hwnd, gpm_state_t *state) {
    HWND list = GetDlgItem(hwnd, IDC_GPM_LIST);
    int id = gpm_selected_game_id(list);
    if (id == 0) {
        return;
    }

    game_profile_t *gp = gpm_find_game_by_id(state->store, id);
    if (!gp) {
        return;
    }

    game_profile_t copy = *gp;
    if (dialog_edit_game_profile_show(hwnd, &copy, false) == IDOK) {
        if (profile_store_update_game(state->store, id, &copy)) {
            gpm_persist(state->store, state->ini_path);
            gpm_refresh_list(list, state->store);
        }
    }
}

/* Handle the Delete button: confirm cascade and then delete. */
static void gpm_handle_delete(HWND hwnd, gpm_state_t *state) {
    HWND list = GetDlgItem(hwnd, IDC_GPM_LIST);
    int id = gpm_selected_game_id(list);
    if (id == 0) {
        return;
    }

    game_profile_t *gp = gpm_find_game_by_id(state->store, id);
    if (!gp) {
        return;
    }

    size_t child_count = gpm_count_children(state->store, id);
    wchar_t msg[512];

    _snwprintf(msg, 512, L"%ls\r\n\r\n%ls: %ls\r\n%ls: %zu",
        praxis_locale_str(STR_PRAXIS_CONFIRM_DELETE_GAME),
        praxis_locale_str(STR_PRAXIS_PROFILE_NAME),
        gp->name,
        praxis_locale_str(STR_PRAXIS_BACKUP_PROFILE),
        child_count);
    msg[511] = L'\0';

    int rc = MessageBoxW(hwnd, msg, praxis_locale_str(STR_PRAXIS_CONFIRM),
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (rc != IDYES) {
        return;
    }

    if (profile_store_delete_game(state->store, id)) {
        gpm_persist(state->store, state->ini_path);
        gpm_refresh_list(list, state->store);
    }
}

static INT_PTR CALLBACK gpm_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    gpm_state_t *state = (gpm_state_t *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)lp);
        state = (gpm_state_t *)lp;
        SetWindowTextW(hwnd, praxis_locale_str(STR_PRAXIS_MANAGE_GAME_PROFILES));
        SetDlgItemTextW(hwnd, IDC_GPM_ADD,    praxis_locale_str(STR_PRAXIS_BTN_ADD));
        SetDlgItemTextW(hwnd, IDC_GPM_EDIT,   praxis_locale_str(STR_PRAXIS_BTN_EDIT));
        SetDlgItemTextW(hwnd, IDC_GPM_DELETE, praxis_locale_str(STR_PRAXIS_BTN_DELETE));
        SetDlgItemTextW(hwnd, IDC_GPM_CLOSE,  praxis_locale_str(STR_PRAXIS_BTN_CLOSE));
        gpm_init_columns(GetDlgItem(hwnd, IDC_GPM_LIST));
        if (state) {
            gpm_refresh_list(GetDlgItem(hwnd, IDC_GPM_LIST), state->store);
        }
        return TRUE;

    case WM_COMMAND:
        if (!state) {
            return FALSE;
        }
        switch (LOWORD(wp)) {
        case IDC_GPM_ADD:
            gpm_handle_add(hwnd, state);
            return TRUE;
        case IDC_GPM_EDIT:
            gpm_handle_edit(hwnd, state);
            return TRUE;
        case IDC_GPM_DELETE:
            gpm_handle_delete(hwnd, state);
            return TRUE;
        case IDC_GPM_CLOSE:
        case IDOK:
        case IDCANCEL:
            EndDialog(hwnd, IDOK);
            return TRUE;
        }
        return FALSE;

    case WM_NOTIFY: {
            LPNMHDR nmh = (LPNMHDR)lp;
            if (nmh && nmh->idFrom == IDC_GPM_LIST && nmh->code == NM_DBLCLK && state) {
                gpm_handle_edit(hwnd, state);
                return TRUE;
            }
        }
        return FALSE;

    case WM_CLOSE:
        EndDialog(hwnd, IDOK);
        return TRUE;
    }

    return FALSE;
}

INT_PTR dialog_game_profile_manager_show(HWND parent, profile_store_t *store, const wchar_t *ini_path) {
    gpm_state_t state;

    if (!store) {
        return IDCANCEL;
    }

    state.store = store;
    state.ini_path = ini_path;

    return DialogBoxParamW(GetModuleHandleW(NULL),
        MAKEINTRESOURCEW(IDD_GAME_PROFILE_MANAGER),
        parent, gpm_dlg_proc, (LPARAM)&state);
}
