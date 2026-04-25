/**
 * @file edit_game_profile.c
 * @brief Implementation of the Edit Game Profile modal dialog.
 * @details Captures Name, Game, original_save_dir, and tree_root for a game_profile_t.
 *          Browse buttons use file_dialog_open_folder for folder selection.
 */

#include "edit_game_profile.h"

#include "../locale.h"
#include "../resource.h"
#include "file_dialog.h"

#include <stdbool.h>
#include <wchar.h>
#include <windows.h>

/* Dialog state stored in DWLP_USER. */
typedef struct egp_state_s {
    game_profile_t *gp;
    bool is_new;
} egp_state_t;

/* Populate the Game combobox with all known game IDs. */
static void egp_populate_games(HWND combo, game_id_t selected) {
    int idx = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Elden Ring");
    if (idx >= 0) {
        SendMessageW(combo, CB_SETITEMDATA, idx, (LPARAM)GAME_ID_ELDEN_RING);
        if (selected == GAME_ID_ELDEN_RING) {
            SendMessageW(combo, CB_SETCURSEL, idx, 0);
        }
    }
    /* Future games append here. */

    if (SendMessageW(combo, CB_GETCURSEL, 0, 0) == CB_ERR) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

/* Open a folder picker and write the result into the named edit control. */
static void egp_browse_into(HWND hwnd, int edit_id) {
    wchar_t current[MAX_PATH];
    GetDlgItemTextW(hwnd, edit_id, current, MAX_PATH);

    wchar_t *picked = file_dialog_open_folder(hwnd, current[0] ? current : NULL);
    if (picked) {
        SetDlgItemTextW(hwnd, edit_id, picked);
        CoTaskMemFree(picked);
    }
}

/* Validate user input and copy to gp_inout. Returns false if validation failed. */
static bool egp_commit(HWND hwnd, egp_state_t *state) {
    wchar_t name[64];
    wchar_t save_dir[MAX_PATH];
    wchar_t tree_root[MAX_PATH];

    GetDlgItemTextW(hwnd, IDC_EGP_NAME, name, 64);
    GetDlgItemTextW(hwnd, IDC_EGP_SAVE_DIR, save_dir, MAX_PATH);
    GetDlgItemTextW(hwnd, IDC_EGP_TREE_ROOT, tree_root, MAX_PATH);

    if (name[0] == L'\0' || tree_root[0] == L'\0') {
        MessageBoxW(hwnd, praxis_locale_str(STR_PRAXIS_ERROR),
            praxis_locale_str(STR_PRAXIS_ERROR), MB_OK | MB_ICONERROR);
        return false;
    }

    HWND combo = GetDlgItem(hwnd, IDC_EGP_GAME);
    int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    game_id_t gid = GAME_ID_ELDEN_RING;
    if (sel != CB_ERR) {
        LRESULT data = SendMessageW(combo, CB_GETITEMDATA, sel, 0);
        if (data != CB_ERR) {
            gid = (game_id_t)data;
        }
    }

    lstrcpynW(state->gp->name, name, 64);
    lstrcpynW(state->gp->original_save_dir, save_dir, MAX_PATH);
    lstrcpynW(state->gp->tree_root, tree_root, MAX_PATH);
    state->gp->game_id = gid;
    return true;
}

static INT_PTR CALLBACK egp_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    egp_state_t *state = (egp_state_t *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)lp);
        state = (egp_state_t *)lp;
        if (!state || !state->gp) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }

        SetWindowTextW(hwnd, praxis_locale_str(STR_PRAXIS_GAME_PROFILE));
        SetDlgItemTextW(hwnd, IDOK,     praxis_locale_str(STR_PRAXIS_BTN_OK));
        SetDlgItemTextW(hwnd, IDCANCEL, praxis_locale_str(STR_PRAXIS_BTN_CANCEL));

        if (!state->is_new) {
            SetDlgItemTextW(hwnd, IDC_EGP_NAME, state->gp->name);
            SetDlgItemTextW(hwnd, IDC_EGP_SAVE_DIR, state->gp->original_save_dir);
            SetDlgItemTextW(hwnd, IDC_EGP_TREE_ROOT, state->gp->tree_root);
        }

        egp_populate_games(GetDlgItem(hwnd, IDC_EGP_GAME), state->gp->game_id);

        SendMessageW(GetDlgItem(hwnd, IDC_EGP_NAME), EM_LIMITTEXT, 63, 0);
        SendMessageW(GetDlgItem(hwnd, IDC_EGP_SAVE_DIR), EM_LIMITTEXT, MAX_PATH - 1, 0);
        SendMessageW(GetDlgItem(hwnd, IDC_EGP_TREE_ROOT), EM_LIMITTEXT, MAX_PATH - 1, 0);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_EGP_BROWSE_SAVE:
            egp_browse_into(hwnd, IDC_EGP_SAVE_DIR);
            return TRUE;
        case IDC_EGP_BROWSE_TREE:
            egp_browse_into(hwnd, IDC_EGP_TREE_ROOT);
            return TRUE;
        case IDOK:
            if (state && egp_commit(hwnd, state)) {
                EndDialog(hwnd, IDOK);
            }
            return TRUE;
        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        return FALSE;

    case WM_CLOSE:
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
    }

    return FALSE;
}

INT_PTR edit_game_profile(HWND parent, game_profile_t *gp_inout, bool is_new) {
    egp_state_t state;

    if (!gp_inout) {
        return IDCANCEL;
    }

    state.gp = gp_inout;
    state.is_new = is_new;

    return DialogBoxParamW(GetModuleHandleW(NULL),
        MAKEINTRESOURCEW(IDD_EDIT_GAME_PROFILE),
        parent, egp_dlg_proc, (LPARAM)&state);
}
