/**
 * @file migration_wizard.c
 * @brief Implementation of the 4-page first-launch migration wizard.
 * @details Walks the user through Welcome -> Game name -> Backup details -> Confirm,
 *          then calls profile_store_migrate to convert the legacy INI in place.
 *          If the user cancels (Cancel button or X), the legacy INI is untouched and the
 *          wizard will run again on next launch.
 */

#include "migration_wizard.h"

#include "../game_backend.h"
#include "../locale.h"
#include "../resource.h"
#include "file_dialog.h"

#include <stdbool.h>
#include <wchar.h>
#include <windows.h>

/* Wizard page constants. */
#define MW_PAGE_WELCOME 0
#define MW_PAGE_GAME    1
#define MW_PAGE_BACKUP  2
#define MW_PAGE_CONFIRM 3

/* Dialog state. */
typedef struct mw_state_s {
    profile_store_t *store;
    const wchar_t *ini_path;
    const wchar_t *legacy_tree_root;
    int legacy_compression_level;
    int page;
    bool finished;
    /* Form values (snapshot when leaving each page). */
    wchar_t game_name[64];
    wchar_t backup_name[64];
    wchar_t backup_tree_root[MAX_PATH];
    compression_level_t comp;
} mw_state_t;

/* Map legacy LZMA level (1..9) to coarse compression_level_t. */
static compression_level_t mw_map_legacy_comp(int lvl) {
    if (lvl <= 1) return COMP_LEVEL_NONE;
    if (lvl <= 4) return COMP_LEVEL_LOW;
    if (lvl <= 7) return COMP_LEVEL_MEDIUM;
    return COMP_LEVEL_HIGH;
}

/* Set the visibility of a dialog control. */
static void mw_show(HWND hwnd, int id, bool visible) {
    HWND ctrl = GetDlgItem(hwnd, id);
    if (ctrl) {
        ShowWindow(ctrl, visible ? SW_SHOW : SW_HIDE);
        EnableWindow(ctrl, visible ? TRUE : FALSE);
    }
}

/* Apply the radio compression selection. */
static void mw_set_compression(HWND hwnd, compression_level_t level) {
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

/* Read the radio compression selection. */
static compression_level_t mw_get_compression(HWND hwnd) {
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

/* Refresh the dialog UI for the current page. */
static void mw_apply_page(HWND hwnd, mw_state_t *state) {
    bool show_name      = false;
    bool show_tree_root = false;
    bool show_comp      = false;
    bool show_back      = false;
    bool show_next      = false;
    bool show_finish    = false;
    const wchar_t *label_text = L"";
    wchar_t summary_buf[1024];

    switch (state->page) {
    case MW_PAGE_WELCOME:
        label_text = praxis_locale_str(STR_PRAXIS_MIGRATION_WELCOME);
        show_next = true;
        break;
    case MW_PAGE_GAME:
        label_text = praxis_locale_str(STR_PRAXIS_MIGRATION_GAME_PAGE);
        show_name = true;
        show_back = true;
        show_next = true;
        SetDlgItemTextW(hwnd, IDC_MW_NAME, state->game_name);
        break;
    case MW_PAGE_BACKUP:
        label_text = praxis_locale_str(STR_PRAXIS_MIGRATION_BACKUP_PAGE);
        show_name = true;
        show_tree_root = true;
        show_comp = true;
        show_back = true;
        show_next = true;
        SetDlgItemTextW(hwnd, IDC_MW_NAME, state->backup_name);
        SetDlgItemTextW(hwnd, IDC_MW_TREE_ROOT, state->backup_tree_root);
        mw_set_compression(hwnd, state->comp);
        break;
    case MW_PAGE_CONFIRM:
        _snwprintf(summary_buf, 1024,
            L"%ls\r\n\r\n"
            L"%ls: %ls\r\n"
            L"%ls: %ls\r\n"
            L"%ls: %ls",
            praxis_locale_str(STR_PRAXIS_MIGRATION_CONFIRM),
            praxis_locale_str(STR_PRAXIS_GAME_PROFILE), state->game_name,
            praxis_locale_str(STR_PRAXIS_BACKUP_PROFILE), state->backup_name,
            praxis_locale_str(STR_PRAXIS_PROFILE_TREE_ROOT), state->backup_tree_root);
        summary_buf[1023] = L'\0';
        SetDlgItemTextW(hwnd, IDC_MW_LABEL, summary_buf);
        show_back = true;
        show_finish = true;
        break;
    }

    if (state->page != MW_PAGE_CONFIRM) {
        SetDlgItemTextW(hwnd, IDC_MW_LABEL, label_text);
    }

    mw_show(hwnd, IDC_MW_NAME, show_name);
    mw_show(hwnd, IDC_MW_TREE_ROOT, show_tree_root);
    mw_show(hwnd, IDC_MW_BROWSE, show_tree_root);
    mw_show(hwnd, IDC_MW_COMP_GROUP,   show_comp);
    mw_show(hwnd, IDC_EBP_COMP_NONE,   show_comp);
    mw_show(hwnd, IDC_EBP_COMP_LOW,    show_comp);
    mw_show(hwnd, IDC_EBP_COMP_MEDIUM, show_comp);
    mw_show(hwnd, IDC_EBP_COMP_HIGH,   show_comp);
    mw_show(hwnd, IDC_MW_BACK, show_back);
    mw_show(hwnd, IDC_MW_NEXT, show_next);
    mw_show(hwnd, IDC_MW_FINISH, show_finish);
}

/* Capture form values from current page into state. Returns false if validation fails. */
static bool mw_capture_page(HWND hwnd, mw_state_t *state) {
    wchar_t buf[MAX_PATH];

    switch (state->page) {
    case MW_PAGE_GAME:
        GetDlgItemTextW(hwnd, IDC_MW_NAME, buf, 64);
        if (buf[0] == L'\0') {
            MessageBoxW(hwnd, praxis_locale_str(STR_PRAXIS_ERROR),
                praxis_locale_str(STR_PRAXIS_ERROR), MB_OK | MB_ICONERROR);
            return false;
        }
        lstrcpynW(state->game_name, buf, 64);
        return true;
    case MW_PAGE_BACKUP:
        GetDlgItemTextW(hwnd, IDC_MW_NAME, buf, 64);
        if (buf[0] == L'\0') {
            MessageBoxW(hwnd, praxis_locale_str(STR_PRAXIS_ERROR),
                praxis_locale_str(STR_PRAXIS_ERROR), MB_OK | MB_ICONERROR);
            return false;
        }
        lstrcpynW(state->backup_name, buf, 64);

        GetDlgItemTextW(hwnd, IDC_MW_TREE_ROOT, buf, MAX_PATH);
        if (buf[0] == L'\0') {
            MessageBoxW(hwnd, praxis_locale_str(STR_PRAXIS_ERROR),
                praxis_locale_str(STR_PRAXIS_ERROR), MB_OK | MB_ICONERROR);
            return false;
        }
        lstrcpynW(state->backup_tree_root, buf, MAX_PATH);
        state->comp = mw_get_compression(hwnd);
        return true;
    default:
        return true;
    }
}

/* Run the migration. Returns true on success. */
static bool mw_perform_migration(mw_state_t *state) {
    return profile_store_migrate(state->store, state->ini_path,
        state->game_name, state->backup_name,
        GAME_ID_ELDEN_RING, state->comp,
        state->ini_path);
}

/* Open a folder picker for the backup tree root field. */
static void mw_browse(HWND hwnd) {
    wchar_t current[MAX_PATH];
    GetDlgItemTextW(hwnd, IDC_MW_TREE_ROOT, current, MAX_PATH);

    wchar_t *picked = file_dialog_open_folder(hwnd, current[0] ? current : NULL);
    if (picked) {
        SetDlgItemTextW(hwnd, IDC_MW_TREE_ROOT, picked);
        CoTaskMemFree(picked);
    }
}

static INT_PTR CALLBACK mw_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    mw_state_t *state = (mw_state_t *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)lp);
        state = (mw_state_t *)lp;
        if (!state || !state->store || !state->ini_path) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }

        SetWindowTextW(hwnd, praxis_locale_str(STR_PRAXIS_MIGRATION_TITLE));
        SetDlgItemTextW(hwnd, IDC_MW_BACK,    L"< Back");
        SetDlgItemTextW(hwnd, IDC_MW_NEXT,    L"Next >");
        SetDlgItemTextW(hwnd, IDC_MW_FINISH,  praxis_locale_str(STR_PRAXIS_BTN_OK));
        SetDlgItemTextW(hwnd, IDCANCEL,       praxis_locale_str(STR_PRAXIS_BTN_CANCEL));
        SetDlgItemTextW(hwnd, IDC_MW_COMP_GROUP, praxis_locale_str(STR_PRAXIS_PROFILE_COMPRESSION));
        SetDlgItemTextW(hwnd, IDC_EBP_COMP_NONE,   praxis_locale_str(STR_PRAXIS_COMPRESSION_NONE));
        SetDlgItemTextW(hwnd, IDC_EBP_COMP_LOW,    praxis_locale_str(STR_PRAXIS_COMPRESSION_LOW));
        SetDlgItemTextW(hwnd, IDC_EBP_COMP_MEDIUM, praxis_locale_str(STR_PRAXIS_COMPRESSION_MEDIUM));
        SetDlgItemTextW(hwnd, IDC_EBP_COMP_HIGH,   praxis_locale_str(STR_PRAXIS_COMPRESSION_HIGH));

        SendMessageW(GetDlgItem(hwnd, IDC_MW_NAME),      EM_LIMITTEXT, 63, 0);
        SendMessageW(GetDlgItem(hwnd, IDC_MW_TREE_ROOT), EM_LIMITTEXT, MAX_PATH - 1, 0);

        /* Seed defaults. */
        lstrcpyW(state->game_name, L"Default");
        lstrcpyW(state->backup_name, L"Main");
        if (state->legacy_tree_root && state->legacy_tree_root[0] != L'\0') {
            lstrcpynW(state->backup_tree_root, state->legacy_tree_root, MAX_PATH);
        } else {
            state->backup_tree_root[0] = L'\0';
        }
        state->comp = mw_map_legacy_comp(state->legacy_compression_level);

        state->page = MW_PAGE_WELCOME;
        mw_apply_page(hwnd, state);
        return TRUE;

    case WM_COMMAND:
        if (!state) {
            return FALSE;
        }
        switch (LOWORD(wp)) {
        case IDC_MW_BROWSE:
            mw_browse(hwnd);
            return TRUE;
        case IDC_MW_BACK:
            if (state->page > MW_PAGE_WELCOME) {
                state->page--;
                mw_apply_page(hwnd, state);
            }
            return TRUE;
        case IDC_MW_NEXT:
            if (!mw_capture_page(hwnd, state)) {
                return TRUE;
            }
            if (state->page < MW_PAGE_CONFIRM) {
                state->page++;
                mw_apply_page(hwnd, state);
            }
            return TRUE;
        case IDC_MW_FINISH:
        case IDOK:
            if (state->page == MW_PAGE_CONFIRM) {
                if (mw_perform_migration(state)) {
                    state->finished = true;
                    EndDialog(hwnd, IDOK);
                } else {
                    MessageBoxW(hwnd, praxis_locale_str(STR_PRAXIS_ERROR),
                        praxis_locale_str(STR_PRAXIS_ERROR), MB_OK | MB_ICONERROR);
                }
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

bool run_migration_wizard(HWND parent, profile_store_t *store, const wchar_t *ini_path,
    const wchar_t *legacy_tree_root, int legacy_compression_level) {
    mw_state_t state;

    if (!store || !ini_path) {
        return false;
    }

    ZeroMemory(&state, sizeof(state));
    state.store = store;
    state.ini_path = ini_path;
    state.legacy_tree_root = legacy_tree_root;
    state.legacy_compression_level = legacy_compression_level;

    DialogBoxParamW(GetModuleHandleW(NULL),
        MAKEINTRESOURCEW(IDD_MIGRATION_WIZARD),
        parent, mw_dlg_proc, (LPARAM)&state);

    return state.finished;
}
