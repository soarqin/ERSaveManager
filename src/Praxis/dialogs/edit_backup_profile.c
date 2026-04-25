/**
 * @file edit_backup_profile.c
 * @brief Implementation of the Edit Backup Profile modal dialog.
 * @details Captures the backup profile name and compression level.
 */

#include "edit_backup_profile.h"

#include "../locale.h"
#include "../resource.h"

#include <stdbool.h>
#include <windows.h>

/* Dialog state stored in DWLP_USER. */
typedef struct ebp_state_s {
    backup_profile_t *bp;
    bool is_new;
} ebp_state_t;

/* Apply compression-level radio button selection. */
static void ebp_set_compression(HWND hwnd, compression_level_t level) {
    int id;

    switch (level) {
    case COMP_LEVEL_NONE:   id = IDC_EBP_COMP_NONE;   break;
    case COMP_LEVEL_MEDIUM: id = IDC_EBP_COMP_MEDIUM; break;
    case COMP_LEVEL_HIGH:   id = IDC_EBP_COMP_HIGH;   break;
    default:                id = IDC_EBP_COMP_LOW;    break;
    }

    CheckRadioButton(hwnd, IDC_EBP_COMP_NONE, IDC_EBP_COMP_HIGH, id);
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

/* Validate user input and copy to bp_inout. Returns false if validation failed. */
static bool ebp_commit(HWND hwnd, ebp_state_t *state) {
    wchar_t name[64];

    GetDlgItemTextW(hwnd, IDC_EBP_NAME, name, 64);
    if (name[0] == L'\0') {
        MessageBoxW(hwnd, praxis_locale_str(STR_PRAXIS_ERROR),
            praxis_locale_str(STR_PRAXIS_ERROR), MB_OK | MB_ICONERROR);
        return false;
    }

    lstrcpynW(state->bp->name, name, 64);
    state->bp->compression_level = ebp_get_compression(hwnd);
    return true;
}

static INT_PTR CALLBACK ebp_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ebp_state_t *state = (ebp_state_t *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)lp);
        state = (ebp_state_t *)lp;
        if (state == NULL || state->bp == NULL) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }

        SetWindowTextW(hwnd, praxis_locale_str(STR_PRAXIS_BACKUP_PROFILE));
        SetDlgItemTextW(hwnd, IDOK, praxis_locale_str(STR_PRAXIS_BTN_OK));
        SetDlgItemTextW(hwnd, IDCANCEL, praxis_locale_str(STR_PRAXIS_BTN_CANCEL));
        SetDlgItemTextW(hwnd, IDC_EBP_COMP_NONE, praxis_locale_str(STR_PRAXIS_COMPRESSION_NONE));
        SetDlgItemTextW(hwnd, IDC_EBP_COMP_LOW, praxis_locale_str(STR_PRAXIS_COMPRESSION_LOW));
        SetDlgItemTextW(hwnd, IDC_EBP_COMP_MEDIUM, praxis_locale_str(STR_PRAXIS_COMPRESSION_MEDIUM));
        SetDlgItemTextW(hwnd, IDC_EBP_COMP_HIGH, praxis_locale_str(STR_PRAXIS_COMPRESSION_HIGH));

        SendMessageW(GetDlgItem(hwnd, IDC_EBP_NAME), EM_LIMITTEXT, 63, 0);

        if (state->is_new) {
            ebp_set_compression(hwnd, COMP_LEVEL_MEDIUM);
        } else {
            SetDlgItemTextW(hwnd, IDC_EBP_NAME, state->bp->name);
            ebp_set_compression(hwnd, state->bp->compression_level);
        }
        return TRUE;

    case WM_COMMAND:
        if (state == NULL) {
            return FALSE;
        }
        switch (LOWORD(wp)) {
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

INT_PTR dialog_edit_backup_profile_show(HWND parent, backup_profile_t *bp_inout, bool is_new) {
    ebp_state_t state;

    if (bp_inout == NULL) {
        return IDCANCEL;
    }

    state.bp = bp_inout;
    state.is_new = is_new;

    return DialogBoxParamW(GetModuleHandleW(NULL),
        MAKEINTRESOURCEW(IDD_EDIT_BACKUP_PROFILE),
        parent, ebp_dlg_proc, (LPARAM)&state);
}
