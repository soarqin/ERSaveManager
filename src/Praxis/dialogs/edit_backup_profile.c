/**
 * @file edit_backup_profile.c
 * @brief Implementation of the Edit Backup Profile modal dialog.
 * @details Captures Name, tree_root, and compression level for a backup_profile_t.
 *          When creating a new profile, the tree_root is auto-derived from the parent game's
 *          tree_root + the typed name unless the user manually edits the tree_root field.
 */

#include "edit_backup_profile.h"

#include "../locale.h"
#include "../resource.h"
#include "file_dialog.h"

#include <stdbool.h>
#include <wchar.h>
#include <windows.h>

/* Dialog state stored in DWLP_USER. */
typedef struct ebp_state_s {
    backup_profile_t *bp;
    const wchar_t *parent_tree_root;    /* tree_root of parent game profile */
    bool is_new;
    bool user_edited_tree_root;         /* true once the tree_root field was edited manually */
    bool suppress_tree_root_change;     /* true while we are setting tree_root programmatically */
} ebp_state_t;

/* Build "<parent_tree_root>\<name>" into out (auto-suggested tree_root). */
static void ebp_build_default_tree_root(const wchar_t *parent_tree_root, const wchar_t *name,
    wchar_t *out, size_t out_chars) {
    out[0] = L'\0';
    if (!parent_tree_root || !name || !out || out_chars == 0) {
        return;
    }
    if (parent_tree_root[0] == L'\0' || name[0] == L'\0') {
        return;
    }
    _snwprintf(out, out_chars, L"%ls\\%ls", parent_tree_root, name);
    out[out_chars - 1] = L'\0';
}

/* Apply compression-level radio button selection. */
static void ebp_set_compression(HWND hwnd, compression_level_t level) {
    int id;
    switch (level) {
    case COMP_LEVEL_NONE:   id = IDC_EBP_COMP_NONE;   break;
    case COMP_LEVEL_MEDIUM: id = IDC_EBP_COMP_MEDIUM; break;
    case COMP_LEVEL_HIGH:   id = IDC_EBP_COMP_HIGH;   break;
    default:                id = IDC_EBP_COMP_LOW;    break; /* COMP_LEVEL_LOW */
    }
    /* Range: IDC_EBP_COMP_NONE (4204) .. IDC_EBP_COMP_MEDIUM (4207) covers all 4 buttons. */
    CheckRadioButton(hwnd, IDC_EBP_COMP_NONE, IDC_EBP_COMP_MEDIUM, id);
}

/* Read compression-level radio button selection. */
static compression_level_t ebp_get_compression(HWND hwnd) {
    if (IsDlgButtonChecked(hwnd, IDC_EBP_COMP_NONE) == BST_CHECKED) {
        return COMP_LEVEL_NONE;
    }
    if (IsDlgButtonChecked(hwnd, IDC_EBP_COMP_MEDIUM) == BST_CHECKED) {
        return COMP_LEVEL_MEDIUM;
    }
    if (IsDlgButtonChecked(hwnd, IDC_EBP_COMP_HIGH) == BST_CHECKED) {
        return COMP_LEVEL_HIGH;
    }
    return COMP_LEVEL_LOW;
}

/* Open a folder picker and set tree_root. Marks user_edited_tree_root so auto-suggest stops. */
static void ebp_browse_tree_root(HWND hwnd, ebp_state_t *state) {
    wchar_t current[MAX_PATH];
    GetDlgItemTextW(hwnd, IDC_EBP_TREE_ROOT, current, MAX_PATH);

    wchar_t *picked = file_dialog_open_folder(hwnd, current[0] ? current : NULL);
    if (picked) {
        state->suppress_tree_root_change = true;
        SetDlgItemTextW(hwnd, IDC_EBP_TREE_ROOT, picked);
        state->suppress_tree_root_change = false;
        state->user_edited_tree_root = true;
        CoTaskMemFree(picked);
    }
}

/* Validate user input and copy to bp_inout. Returns false if validation failed. */
static bool ebp_commit(HWND hwnd, ebp_state_t *state) {
    wchar_t name[64];
    wchar_t tree_root[MAX_PATH];

    GetDlgItemTextW(hwnd, IDC_EBP_NAME, name, 64);
    GetDlgItemTextW(hwnd, IDC_EBP_TREE_ROOT, tree_root, MAX_PATH);

    if (name[0] == L'\0' || tree_root[0] == L'\0') {
        MessageBoxW(hwnd, praxis_locale_str(STR_PRAXIS_ERROR),
            praxis_locale_str(STR_PRAXIS_ERROR), MB_OK | MB_ICONERROR);
        return false;
    }

    lstrcpynW(state->bp->name, name, 64);
    lstrcpynW(state->bp->tree_root, tree_root, MAX_PATH);
    state->bp->compression_level = ebp_get_compression(hwnd);
    return true;
}

static INT_PTR CALLBACK ebp_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ebp_state_t *state = (ebp_state_t *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)lp);
        state = (ebp_state_t *)lp;
        if (!state || !state->bp) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }

        SetWindowTextW(hwnd, praxis_locale_str(STR_PRAXIS_BACKUP_PROFILE));
        SetDlgItemTextW(hwnd, IDOK,     praxis_locale_str(STR_PRAXIS_BTN_OK));
        SetDlgItemTextW(hwnd, IDCANCEL, praxis_locale_str(STR_PRAXIS_BTN_CANCEL));
        SetDlgItemTextW(hwnd, IDC_EBP_COMP_NONE,   praxis_locale_str(STR_PRAXIS_COMPRESSION_NONE));
        SetDlgItemTextW(hwnd, IDC_EBP_COMP_LOW,    praxis_locale_str(STR_PRAXIS_COMPRESSION_LOW));
        SetDlgItemTextW(hwnd, IDC_EBP_COMP_MEDIUM, praxis_locale_str(STR_PRAXIS_COMPRESSION_MEDIUM));
        SetDlgItemTextW(hwnd, IDC_EBP_COMP_HIGH,   praxis_locale_str(STR_PRAXIS_COMPRESSION_HIGH));

        SendMessageW(GetDlgItem(hwnd, IDC_EBP_NAME),      EM_LIMITTEXT, 63, 0);
        SendMessageW(GetDlgItem(hwnd, IDC_EBP_TREE_ROOT), EM_LIMITTEXT, MAX_PATH - 1, 0);

        if (state->is_new) {
            ebp_set_compression(hwnd, COMP_LEVEL_LOW);
            /* tree_root remains empty until user starts typing a name. */
            state->user_edited_tree_root = false;
        } else {
            SetDlgItemTextW(hwnd, IDC_EBP_NAME, state->bp->name);
            state->suppress_tree_root_change = true;
            SetDlgItemTextW(hwnd, IDC_EBP_TREE_ROOT, state->bp->tree_root);
            state->suppress_tree_root_change = false;
            ebp_set_compression(hwnd, state->bp->compression_level);
            state->user_edited_tree_root = true; /* preserve existing tree_root verbatim */
        }
        return TRUE;

    case WM_COMMAND:
        if (!state) {
            return FALSE;
        }
        switch (LOWORD(wp)) {
        case IDC_EBP_BROWSE:
            ebp_browse_tree_root(hwnd, state);
            return TRUE;
        case IDC_EBP_NAME:
            if (HIWORD(wp) == EN_CHANGE && state->is_new && !state->user_edited_tree_root) {
                wchar_t name[64];
                wchar_t suggestion[MAX_PATH];

                GetDlgItemTextW(hwnd, IDC_EBP_NAME, name, 64);
                ebp_build_default_tree_root(state->parent_tree_root, name, suggestion, MAX_PATH);
                state->suppress_tree_root_change = true;
                SetDlgItemTextW(hwnd, IDC_EBP_TREE_ROOT, suggestion);
                state->suppress_tree_root_change = false;
            }
            return TRUE;
        case IDC_EBP_TREE_ROOT:
            if (HIWORD(wp) == EN_CHANGE && !state->suppress_tree_root_change) {
                state->user_edited_tree_root = true;
            }
            return TRUE;
        case IDOK:
            if (ebp_commit(hwnd, state)) {
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

INT_PTR edit_backup_profile(HWND parent, backup_profile_t *bp_inout,
    const wchar_t *parent_game_tree_root, bool is_new) {
    ebp_state_t state;

    if (!bp_inout) {
        return IDCANCEL;
    }

    state.bp = bp_inout;
    state.parent_tree_root = parent_game_tree_root;
    state.is_new = is_new;
    state.user_edited_tree_root = false;
    state.suppress_tree_root_change = false;

    return DialogBoxParamW(GetModuleHandleW(NULL),
        MAKEINTRESOURCEW(IDD_EDIT_BACKUP_PROFILE),
        parent, ebp_dlg_proc, (LPARAM)&state);
}
