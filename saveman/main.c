#include "resource.h"

#include "config.h"
#include "ersave.h"
#include "locale.h"

#include "embedded_face_data.h"
#include "file_dialog.h"
#include "ui_controls.h"

#include <stdint.h>
#include <wchar.h>

#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>

#define VERSION_STR L"1.1.0"

#define MAIN_WINDOW_CLASS L"ER_SAVE_FACE_MANAGER"

/*** Constants for detail panel ***/
#define STAT_COUNT 8

/*** Global variables ***/

/** @brief Global window handle for the main application window */
HWND main_window;

/*** Top-row controls ***/
/** @brief "Change Folder" button — opens folder picker dialog */
HWND button_change_folder;
/** @brief ComboBox showing available Steam save subfolders */
HWND combo_box_save_folder;
/** @brief "Manage Faces" button — opens face data dialog */
HWND button_manage_faces;

/*** Characters panel (left side) ***/
/** @brief Section label above the characters ListView */
HWND label_chars;
/** @brief ListView displaying all 10 character slots */
HWND list_view_chars;

/*** Faces panel (right side) ***/
/** @brief ListView displaying face data for the selected character */
HWND list_view_faces;

/*** Detail panel (right side) — per-character attribute display ***/
/** @brief Group box enclosing the attribute detail panel */
HWND detail_group;
/** @brief Static labels showing attribute names (Vigor, Mind, …) */
HWND detail_stat_labels[STAT_COUNT];
/** @brief Static labels showing attribute values */
HWND detail_stat_values[STAT_COUNT];

/*** Menu handles ***/
HMENU menu_bar = NULL;
/** @brief Dynamically built submenu for built-in NPC face presets */
HMENU embedded_face_data_menu = NULL;

/*** Shared resources ***/
/** @brief Default message font used by all controls */
HFONT default_font;

/*** Application data ***/
/** @brief Currently loaded save file data; NULL when no file is loaded */
er_save_data_t *save_data = NULL;

/* Mapping from stat index to locale string index */
static const locale_string_index_t stat_str_indices[STAT_COUNT] = {
    STR_VIGOR, STR_MIND, STR_ENDURANCE, STR_STRENGTH,
    STR_DEXTERITY, STR_INTELLIGENCE, STR_FAITH, STR_ARCANE
};

void update_char_list_view(int item, const er_char_data_t *char_data);
static void update_detail_panel(int slot);

/* add_folders_to_combo_box is defined in ui_controls.c */
extern void add_folders_to_combo_box(void);

bool handle_save_folder_selection(HWND hwnd) {
    /* Get selected Steam ID */
    int index = SendMessageW(combo_box_save_folder, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR) {
        lstrcpyW(config.save_subfolder, L"");
        return false;
    }

    wchar_t save_subfolder[32];
    SendMessageW(combo_box_save_folder, CB_GETLBTEXT, index, (LPARAM)save_subfolder);

    /* Build save file path */
    wchar_t save_path[MAX_PATH];
    lstrcpyW(save_path, config.save_path);
    PathAppendW(save_path, save_subfolder);
    PathAppendW(save_path, L"\\ER0000.sl2");

    /* Load new save data */
    er_save_data_t *new_save_data = er_save_data_load(save_path);
    if (!new_save_data) {
        int idx = SendMessageW(combo_box_save_folder, CB_FINDSTRING, -1, (LPARAM)config.save_subfolder);
        SendMessageW(combo_box_save_folder, CB_SETCURSEL, idx == CB_ERR ? 0 : idx, 0);
        MessageBoxW(hwnd, locale_str(STR_FAILED_LOAD_SAVE), locale_str(STR_ERROR), MB_OK | MB_ICONERROR);
        return false;
    }

    /* Check if Steam ID matches */
    uint64_t user_id = er_save_get_userid(new_save_data);
    uint64_t folder_steam_user_id = wcstoull(save_subfolder, NULL, 10);
    if (user_id != folder_steam_user_id) {
        if (MessageBoxW(hwnd, locale_str(STR_STEAM_ID_MISMATCH), locale_str(STR_ERROR), MB_YESNO | MB_ICONWARNING) == IDNO) {
            int idx = SendMessageW(combo_box_save_folder, CB_FINDSTRING, -1, (LPARAM)config.save_subfolder);
            SendMessageW(combo_box_save_folder, CB_SETCURSEL, idx == CB_ERR ? 0 : idx, 0);
            return false;
        }
        er_save_resign_userid(new_save_data, folder_steam_user_id);
    }

    /* Free previous save data if exists */
    if (save_data) {
        er_save_data_free(save_data);
    }
    save_data = new_save_data;

    lstrcpyW(config.save_subfolder, save_subfolder);

    /* Clear characters ListView */
    ListView_DeleteAllItems(list_view_chars);

    /* Add characters to characters ListView */
    for (int i = 0; i < 10; i++) {
        const er_char_data_t *char_data = er_char_data_ref(save_data, i);
        wchar_t text[16];
        wsprintfW(text, L"%d", i + 1);
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = text;
        lvi.iItem = ListView_InsertItem(list_view_chars, &lvi);
        update_char_list_view(lvi.iItem, char_data);
    }

    return true;
}

static void open_dir_dialog_for_new_save_location(HWND hwnd) {
    PWSTR pszPath = file_dialog_open_folder(hwnd, config.save_path);
    if (!pszPath) {
        return;
    }
    lstrcpyW(config.save_path, pszPath);
    CoTaskMemFree(pszPath);

    add_folders_to_combo_box();
    SendMessageW(combo_box_save_folder, CB_SETCURSEL, 0, 0);
    if (handle_save_folder_selection(hwnd)) {
        save_config();
    }
}

static void on_import_embedded_face_data(HWND hwnd, int idx, int item) {
    if (idx < 0 || idx >= embedded_face_data_count) {
        return;
    }
    const uint8_t *face_data = embedded_face_data[idx].data;
    if (face_data && er_face_data_import(save_data, item, face_data)) {
        uint8_t available, gender;
        er_face_data_info(face_data, &available, &gender);
        wchar_t body_type[32];
        wsprintfW(body_type, L"%s", locale_str(available ? (gender ? STR_TYPE_B : STR_TYPE_A) : STR_EMPTY));
        ListView_SetItemText(list_view_faces, item, 1, body_type);
        MessageBoxW(hwnd, locale_str(STR_IMPORT_SUCCESS), locale_str(STR_SUCCESS), MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwnd, locale_str(STR_IMPORT_FAILED), locale_str(STR_ERROR), MB_OK | MB_ICONERROR);
    }
}

static void on_menu_change_language(int idx) {
    if (idx == get_current_locale() || idx < 0 || idx >= locale_count()) {
        return;
    }

    /* Uncheck current locale menu item */
    CheckMenuItem(menu_bar, IDM_LOCALE_START + get_current_locale(), MF_UNCHECKED);

    /* Locale menu item selected */
    set_current_locale(idx);

    /* Tick menu item */
    CheckMenuItem(menu_bar, IDM_LOCALE_START + idx, MF_CHECKED);

    /* Save new language to config */
    save_config();

    /* Refresh all UI strings for the new locale */
    ui_refresh_language();
}

/* Function to import face data from a file */
static void import_face_data(HWND hwnd, int item) {
    COMDLG_FILTERSPEC rgSpec[] = {
        { locale_str(STR_ALL_FILES), L"*.*" }
    };
    PWSTR pszPath = file_dialog_open(hwnd, locale_str(STR_IMPORT_FACE_DATA), rgSpec, 1);
    if (pszPath) {
        uint8_t *face_data = er_face_data_from_file(pszPath);
        if (face_data && er_face_data_import(save_data, item, face_data)) {
            uint8_t available, gender;
            er_face_data_info(face_data, &available, &gender);
            wchar_t body_type[32];
            wsprintfW(body_type, L"%s", locale_str(available ? (gender ? STR_TYPE_B : STR_TYPE_A) : STR_EMPTY));
            ListView_SetItemText(list_view_faces, item, 1, body_type);
            MessageBoxW(hwnd, locale_str(STR_IMPORT_SUCCESS), locale_str(STR_SUCCESS), MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(hwnd, locale_str(STR_IMPORT_FAILED), locale_str(STR_ERROR), MB_OK | MB_ICONERROR);
        }
        er_face_data_free(face_data);
        CoTaskMemFree(pszPath);
    }
}

/* Function to export face data to a file */
static void export_face_data(HWND hwnd, int item) {
    COMDLG_FILTERSPEC rgSpec[] = {
        { locale_str(STR_ALL_FILES), L"*.*" }
    };
    PWSTR pszPath = file_dialog_save(hwnd, locale_str(STR_EXPORT_FACE_DATA), rgSpec, 1);
    if (pszPath) {
        const uint8_t *face_data = er_face_data_ref(save_data, item);
        if (face_data && er_face_data_to_file(face_data, pszPath)) {
            MessageBoxW(hwnd, locale_str(STR_EXPORT_SUCCESS), locale_str(STR_SUCCESS), MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(hwnd, locale_str(STR_EXPORT_FAILED), locale_str(STR_ERROR), MB_OK | MB_ICONERROR);
        }
        CoTaskMemFree(pszPath);
    }
}

static bool is_full_save_file(const wchar_t *path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    char tag[4];
    DWORD bytes_read;
    if (!ReadFile(file, tag, 4, &bytes_read, NULL)) {
        CloseHandle(file);
        return false;
    }
    CloseHandle(file);
    return bytes_read == 4 && RtlCompareMemory(tag, "BND4", 4) == 4;
}

void update_char_list_view(int item, const er_char_data_t *char_data) {
    wchar_t text[64];

    if (!char_data) {
        wsprintfW(text, L"%s", locale_str(STR_EMPTY));
        ListView_SetItemText(list_view_chars, item, 1, text);
        return;
    }

    wsprintfW(text, L"%s", er_char_data_get_name(char_data));
    ListView_SetItemText(list_view_chars, item, 1, text);

    uint32_t in_game_time = 0;
    uint8_t gender = 0;
    int level = 0;
    int stats[8] = {0};
    er_char_data_info(char_data, &in_game_time, &gender, &level, stats);

    wsprintfW(text, L"%s", locale_str(gender ? STR_TYPE_A : STR_TYPE_B));
    ListView_SetItemText(list_view_chars, item, 2, text);

    wsprintfW(text, L"%d", level);
    ListView_SetItemText(list_view_chars, item, 3, text);

    in_game_time /= 1000;
    if (in_game_time < 3600) {
        wsprintfW(text, L"%02d:%02d", in_game_time / 60, in_game_time % 60);
    } else {
        wsprintfW(text, L"%02d:%02d:%02d", in_game_time / 3600, (in_game_time % 3600) / 60, in_game_time % 60);
    }
    ListView_SetItemText(list_view_chars, item, 4, text);
}

/* Update the detail panel with attribute info for the selected character slot */
static void update_detail_panel(int slot) {
    if (slot < 0 || !save_data) {
        /* Clear all values */
        for (int i = 0; i < STAT_COUNT; i++) {
            SetWindowTextW(detail_stat_values[i], L"");
        }
        return;
    }

    const er_char_data_t *char_data = er_char_data_ref(save_data, slot);
    if (!char_data) {
        for (int i = 0; i < STAT_COUNT; i++) {
            SetWindowTextW(detail_stat_values[i], L"-");
        }
        return;
    }

    uint32_t in_game_time = 0;
    uint8_t gender = 0;
    int level = 0;
    int stats[STAT_COUNT] = {0};
    er_char_data_info(char_data, &in_game_time, &gender, &level, stats);

    wchar_t text[16];
    for (int i = 0; i < STAT_COUNT; i++) {
        wsprintfW(text, L"%d", stats[i]);
        SetWindowTextW(detail_stat_values[i], text);
    }
}

/* Function to import character data from a file */
static void import_char_data(HWND hwnd, int item) {
    COMDLG_FILTERSPEC rgSpec[] = {
        { locale_str(STR_ALL_FILES), L"*.*" }
    };
    PWSTR pszPath = file_dialog_open(hwnd, locale_str(STR_IMPORT_CHARACTER), rgSpec, 1);
    if (pszPath) {
        if (is_full_save_file(pszPath)) {
            er_save_simple_data_t *simple_save_data = er_save_simple_data_load(pszPath);
            if (simple_save_data) {
                /* Use TaskDialog to select a character slot for import */
                TASKDIALOG_BUTTON buttons[10] = {0};
                int button_count = 0;
                for (int i = 0; i < 10; i++) {
                    const wchar_t *name = er_save_simple_data_get_char_name(simple_save_data, i);
                    if (name == NULL || name[0] == L'\0') {
                        continue;
                    }
                    buttons[button_count].nButtonID = i;
                    buttons[button_count++].pszButtonText = name;
                }
                if (button_count == 0) {
                    MessageBoxW(hwnd, locale_str(STR_NO_CHARACTER_FOUND), locale_str(STR_ERROR), MB_OK | MB_ICONERROR);
                } else {
                    TASKDIALOGCONFIG task_dialog_config;
                    ZeroMemory(&task_dialog_config, sizeof(TASKDIALOGCONFIG));
                    task_dialog_config.cbSize = sizeof(TASKDIALOGCONFIG);
                    task_dialog_config.pszWindowTitle = locale_str(STR_IMPORT_CHARACTER);
                    task_dialog_config.pszContent = locale_str(STR_SELECT_CHARACTER_CONTENT);
                    task_dialog_config.nDefaultButton = IDCANCEL;
                    task_dialog_config.dwCommonButtons = TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON;

                    task_dialog_config.pRadioButtons = buttons;
                    task_dialog_config.cRadioButtons = button_count;

                    int button_id = 0, radio_id = 0;
                    HRESULT hr = TaskDialogIndirect(&task_dialog_config, &button_id, &radio_id, NULL);
                    if (SUCCEEDED(hr) && button_id == IDOK) {
                        uint8_t *char_data;
                        if ((char_data = er_save_simple_data_slot_export(simple_save_data, radio_id)) != NULL) {
                            if (er_char_data_import_raw(save_data, item, char_data)) {
                                update_char_list_view(item, er_char_data_ref(save_data, item));
                                MessageBoxW(hwnd, locale_str(STR_CHARACTER_IMPORT_SUCCESS), locale_str(STR_SUCCESS), MB_OK | MB_ICONINFORMATION);
                            } else {
                                MessageBoxW(hwnd, locale_str(STR_CHARACTER_IMPORT_FAILED), locale_str(STR_ERROR), MB_OK | MB_ICONERROR);
                            }
                            er_save_simple_data_slot_free(char_data);
                        } else {
                            MessageBoxW(hwnd, locale_str(STR_CHARACTER_IMPORT_FAILED), locale_str(STR_ERROR), MB_OK | MB_ICONERROR);
                        }
                    } else {
                        MessageBoxW(hwnd, locale_str(STR_CHARACTER_IMPORT_FAILED), locale_str(STR_ERROR), MB_OK | MB_ICONERROR);
                    }
                }
                er_save_simple_data_free(simple_save_data);
            }
        } else {
            er_char_data_t *char_data = er_char_data_from_file(pszPath);
            if (char_data && er_char_data_import(save_data, item, char_data)) {
                update_char_list_view(item, char_data);
                MessageBoxW(hwnd, locale_str(STR_CHARACTER_IMPORT_SUCCESS), locale_str(STR_SUCCESS), MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd, locale_str(STR_CHARACTER_IMPORT_FAILED), locale_str(STR_ERROR), MB_OK | MB_ICONERROR);
            }
            er_char_data_free(char_data);
        }
        CoTaskMemFree(pszPath);
    }
}

/* Function to export character data to a file */
static void export_char_data(HWND hwnd, int item) {
    COMDLG_FILTERSPEC rgSpec[] = {
        { locale_str(STR_ALL_FILES), L"*.*" }
    };
    PWSTR pszPath = file_dialog_save(hwnd, locale_str(STR_EXPORT_CHARACTER), rgSpec, 1);
    if (pszPath) {
        const er_char_data_t *char_data = er_char_data_ref(save_data, item);
        if (char_data && er_char_data_to_file(char_data, pszPath)) {
            MessageBoxW(hwnd, locale_str(STR_CHARACTER_EXPORT_SUCCESS), locale_str(STR_SUCCESS), MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(hwnd, locale_str(STR_CHARACTER_EXPORT_FAILED), locale_str(STR_ERROR), MB_OK | MB_ICONERROR);
        }
        CoTaskMemFree(pszPath);
    }
}

static LRESULT CALLBACK rename_char_data_dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_INITDIALOG:
            SetWindowTextW(hwnd, locale_str(STR_RENAME_CHARACTER));
            SetWindowTextW(GetDlgItem(hwnd, IDC_STATIC_CHARACTER_NAME), locale_str(STR_ENTER_NEW_NAME));
            SetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_CHARACTER_NAME), (LPWSTR)lparam);
            SetWindowTextW(GetDlgItem(hwnd, IDOK), locale_str(STR_CONFIRM));
            SetWindowTextW(GetDlgItem(hwnd, IDCANCEL), locale_str(STR_CANCEL));
            Edit_LimitText(GetDlgItem(hwnd, IDC_EDIT_CHARACTER_NAME), 16);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDOK: {
                    int item = ListView_GetNextItem(list_view_chars, -1, LVNI_SELECTED);
                    if (item >= 0) {
                        wchar_t new_name[32];
                        Edit_GetText(GetDlgItem(hwnd, IDC_EDIT_CHARACTER_NAME), new_name, sizeof(new_name) / sizeof(wchar_t));
                        if (er_char_data_set_name(save_data, item, new_name)) {
                            ListView_SetItemText(list_view_chars, item, 1, new_name);
                        } else {
                            MessageBoxW(hwnd, locale_str(STR_RENAME_CHARACTER_FAILED), locale_str(STR_ERROR), MB_OK | MB_ICONERROR);
                        }
                    }
                    EndDialog(hwnd, TRUE);
                    break;
                }
                case IDCANCEL: {
                    EndDialog(hwnd, FALSE);
                    break;
                }
            }
    }
    return FALSE;
}

static void rename_char_data(HWND hwnd, int item) {
    if (item == -1) return;
    const er_char_data_t *char_data = er_char_data_ref(save_data, item);
    if (char_data) {
        DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_RENAME_CHARACTER), hwnd, (DLGPROC)rename_char_data_dialog_proc, (LPARAM)er_char_data_get_name(char_data));
    }
}

/* Function to handle characters ListView popup menu */
static void list_view_chars_popup_menu(HWND hwnd, WPARAM wparam, LPARAM lparam) {
    /* Get the item under the cursor */
    POINT pt;
    pt.x = GET_X_LPARAM(lparam);
    pt.y = GET_Y_LPARAM(lparam);
    ScreenToClient(list_view_chars, &pt);

    /* Get the item under the cursor */
    LVHITTESTINFO lvhti = {0};
    lvhti.pt = pt;
    int item = ListView_HitTest(list_view_chars, &lvhti);

    if (item < 0) {
        return;
    }

    /* Create popup menu */
    HMENU menu = CreatePopupMenu();

    if (menu) {
        /* Add menu items */
        AppendMenuW(menu, MF_BYPOSITION | MF_STRING, IDM_IMPORT_CHAR, locale_str(STR_IMPORT_CHARACTER));
        const er_char_data_t *char_data = er_char_data_ref(save_data, item);
        if (char_data) {
            AppendMenuW(menu, MF_BYPOSITION | MF_STRING, IDM_EXPORT_CHAR, locale_str(STR_EXPORT_CHARACTER));
            AppendMenuW(menu, MF_BYPOSITION | MF_STRING, IDM_RENAME_CHAR, locale_str(STR_RENAME_CHARACTER));
        }

        /* Convert window coordinates back to screen coordinates */
        ClientToScreen(list_view_chars, &pt);
        /* Show menu at cursor position */
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(menu);
    }
}

/* Function to handle faces ListView popup menu */
static void list_view_faces_popup_menu(HWND hwnd, WPARAM wparam, LPARAM lparam) {
    /* Get the item under the cursor */
    POINT pt;
    pt.x = GET_X_LPARAM(lparam);
    pt.y = GET_Y_LPARAM(lparam);
    ScreenToClient(list_view_faces, &pt);

    /* Get the item under the cursor */
    LVHITTESTINFO lvhti = {0};
    lvhti.pt = pt;
    int item = ListView_HitTest(list_view_faces, &lvhti);

    if (item < 0) {
        return;
    }

    /* Create popup menu */
    HMENU menu = CreatePopupMenu();

    if (menu) {
        /* Add menu items */
        AppendMenuW(menu, MF_BYPOSITION | MF_STRING, IDM_IMPORT_FACE, locale_str(STR_IMPORT_FACE_DATA));
        AppendMenuW(menu, MF_BYPOSITION | MF_STRING, IDM_EXPORT_FACE, locale_str(STR_EXPORT_FACE_DATA));
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)embedded_face_data_menu, locale_str(STR_IMPORT_NPC_FACE_DATA));

        /* Convert window coordinates back to screen coordinates */
        ClientToScreen(list_view_faces, &pt);
        /* Show menu at cursor position */
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
        RemoveMenu(menu, (UINT_PTR)embedded_face_data_menu, MF_BYCOMMAND);
        DestroyMenu(menu);
    }
}

/* Face data management modal dialog procedure */
static LRESULT CALLBACK face_data_dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_INITDIALOG: {
            HMODULE module = GetModuleHandle(NULL);

            /* Set localized dialog title */
            SetWindowTextW(hwnd, locale_str(STR_FACES));

            /* Create Faces ListView filling the entire client area */
            list_view_faces = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                WC_LISTVIEW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                0, 0, 100, 100,
                hwnd, (HMENU)3, module, NULL
            );
            ListView_SetExtendedListViewStyleEx(list_view_faces,
                LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_BORDERSELECT,
                LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_BORDERSELECT);
            SendMessage(list_view_faces, WM_SETFONT, (WPARAM)default_font, TRUE);

            /* Add columns to Faces ListView */
            LVCOLUMNW lvc;
            lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

            lvc.iSubItem = 0;
            lvc.cx = 80;
            lvc.pszText = (LPWSTR)locale_str(STR_SLOT);
            ListView_InsertColumn(list_view_faces, 0, &lvc);

            lvc.iSubItem = 1;
            lvc.cx = 80;
            lvc.pszText = (LPWSTR)locale_str(STR_BODY_TYPE);
            ListView_InsertColumn(list_view_faces, 1, &lvc);

            /* Populate face data from current save */
            if (save_data) {
                for (int i = 0; i < 15; i++) {
                    const uint8_t *face_data = er_face_data_ref(save_data, i);
                    if (!face_data) continue;

                    uint8_t available, gender;
                    er_face_data_info(face_data, &available, &gender);

                    wchar_t slot_text[32];
                    wsprintfW(slot_text, L"%d", i + 1);
                    wchar_t body_type[32];
                    wsprintfW(body_type, L"%s", locale_str(available ? (gender ? STR_TYPE_B : STR_TYPE_A) : STR_EMPTY));

                    LVITEMW lvi = {0};
                    lvi.mask = LVIF_TEXT;
                    lvi.iItem = i;
                    lvi.iSubItem = 0;
                    lvi.pszText = slot_text;
                    lvi.iItem = ListView_InsertItem(list_view_faces, &lvi);
                    ListView_SetItemText(list_view_faces, lvi.iItem, 1, body_type);
                }
            }

            /* Resize ListView to fill the dialog client area */
            RECT rc;
            GetClientRect(hwnd, &rc);
            MoveWindow(list_view_faces, 0, 0, rc.right, rc.bottom, TRUE);

            return TRUE;
        }

        case WM_SIZE: {
            if (list_view_faces) {
                int width = LOWORD(lparam);
                int height = HIWORD(lparam);
                MoveWindow(list_view_faces, 0, 0, width, height, TRUE);
            }
            return TRUE;
        }

        case WM_CONTEXTMENU: {
            if ((HWND)wparam == list_view_faces) {
                list_view_faces_popup_menu(hwnd, wparam, lparam);
            }
            return TRUE;
        }

        case WM_COMMAND: {
            switch (LOWORD(wparam)) {
                case IDM_IMPORT_FACE: {
                    int item = ListView_GetNextItem(list_view_faces, -1, LVNI_SELECTED);
                    if (item == -1) return TRUE;
                    import_face_data(hwnd, item);
                    break;
                }

                case IDM_EXPORT_FACE: {
                    int item = ListView_GetNextItem(list_view_faces, -1, LVNI_SELECTED);
                    if (item == -1) return TRUE;
                    export_face_data(hwnd, item);
                    break;
                }

                case IDCANCEL: {
                    /* Handle Escape key */
                    list_view_faces = NULL;
                    EndDialog(hwnd, 0);
                    return TRUE;
                }

                default: {
                    int id = LOWORD(wparam);
                    if (id >= IDM_EMBEDDED_FACE_DATA_START && id < IDM_EMBEDDED_FACE_DATA_START + 200) {
                        int item = ListView_GetNextItem(list_view_faces, -1, LVNI_SELECTED);
                        if (item == -1) return TRUE;
                        on_import_embedded_face_data(hwnd, id - IDM_EMBEDDED_FACE_DATA_START, item);
                    }
                    break;
                }
            }
            return TRUE;
        }

        case WM_CLOSE: {
            list_view_faces = NULL;
            EndDialog(hwnd, 0);
            return TRUE;
        }
    }
    return FALSE;
}

/* Function to handle window resize */
static void on_window_resize(HWND hwnd, WPARAM wparam, LPARAM lparam) {
    /* Get window dimensions */
    int width = LOWORD(lparam);
    int height = HIWORD(lparam);

    ui_layout_controls(hwnd, width, height);
}

/* Window procedure */
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE: {
            HMODULE module = GetModuleHandle(NULL);

            /* Create all controls */
            ui_create_controls(hwnd, module);

            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc_static = (HDC)wparam;
            SetBkMode(hdc_static, OPAQUE);
            SetBkColor(hdc_static, GetSysColor(COLOR_WINDOW));
            SetTextColor(hdc_static, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }

        case WM_SIZE: {
            on_window_resize(hwnd, wparam, lparam);
            return 0;
        }

        case WM_NOTIFY: {
            NMHDR *nmhdr = (NMHDR *)lparam;
            if (nmhdr->hwndFrom == list_view_chars && nmhdr->code == LVN_ITEMCHANGED) {
                NMLISTVIEW *nmlv = (NMLISTVIEW *)lparam;
                if (nmlv->uChanged & LVIF_STATE) {
                    if (nmlv->uNewState & LVIS_SELECTED) {
                        /* Item selected - update detail panel */
                        update_detail_panel(nmlv->iItem);
                    } else if ((nmlv->uOldState & LVIS_SELECTED) && !(nmlv->uNewState & LVIS_SELECTED)) {
                        /* Item deselected - clear detail panel if nothing else selected */
                        int sel = ListView_GetNextItem(list_view_chars, -1, LVNI_SELECTED);
                        if (sel == -1) {
                            update_detail_panel(-1);
                        }
                    }
                }
            }
            return 0;
        }

        case WM_CONTEXTMENU: {
            if ((HWND)wparam == list_view_chars) {
                list_view_chars_popup_menu(hwnd, wparam, lparam);
            }
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wparam)) {
                case IDC_BUTTON_CHANGE_FOLDER:
                    open_dir_dialog_for_new_save_location(hwnd);
                    break;

                case IDC_BUTTON_MANAGE_FACES:
                    /* Open face data management as modal dialog */
                    DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_FACE_DATA), hwnd, (DLGPROC)face_data_dialog_proc, 0);
                    break;

                case IDC_COMBO_SAVE_FOLDER: {
                    if (HIWORD(wparam) == CBN_SELCHANGE) {
                        if (handle_save_folder_selection(hwnd)) {
                            /* Save new path to config */
                            save_config();
                        }
                    }
                    break;
                }

                case IDM_IMPORT_CHAR: {
                    /* Get selected item */
                    int item = ListView_GetNextItem(list_view_chars, -1, LVNI_SELECTED);
                    if (item == -1) return 0;
                    import_char_data(hwnd, item);
                    update_detail_panel(item);
                    break;
                }

                case IDM_EXPORT_CHAR: {
                    /* Get selected item */
                    int item = ListView_GetNextItem(list_view_chars, -1, LVNI_SELECTED);
                    if (item == -1) return 0;
                    export_char_data(hwnd, item);
                    break;
                }

                case IDM_RENAME_CHAR: {
                    /* Get selected item */
                    int item = ListView_GetNextItem(list_view_chars, -1, LVNI_SELECTED);
                    if (item == -1) return 0;
                    rename_char_data(hwnd, item);
                    break;
                }

                default: {
                    int id = LOWORD(wparam);
                    if (id >= IDM_LOCALE_START && id < IDM_LOCALE_START + 100) {
                        on_menu_change_language(id - IDM_LOCALE_START);
                    }
                    break;
                }
            }
            return 0;
        }

        case WM_DESTROY:
            /* Save configuration before exiting */
            save_config();
            DestroyMenu(embedded_face_data_menu);
            ui_cleanup();  /* Release shared UI resources */
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* Function to create the main window */
static HWND create_window(HINSTANCE instance, int cmd_show) {
    /* Register window class */
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = MAIN_WINDOW_CLASS;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    RegisterClassExW(&wc);

    wchar_t window_title[64];
    wsprintfW(window_title, L"%s v%s", locale_str(STR_APP_TITLE), VERSION_STR);

    /* Create main window with saved position and size if available */
    HWND hwnd;
    if (config.window_x != -1 && config.window_y != -1 && config.window_width > 0 && config.window_height > 0) {
        hwnd = CreateWindowW(
            MAIN_WINDOW_CLASS, window_title, WS_OVERLAPPEDWINDOW,
            config.window_x, config.window_y, config.window_width, config.window_height,
            NULL, NULL, instance, NULL);
    } else {
        /* Use default position and size */
        hwnd = CreateWindowW(
            MAIN_WINDOW_CLASS, window_title, WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 750, 480,
            NULL, NULL, instance, NULL);
    }

    if (!hwnd)
        return NULL;

    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIconSm);

    /* Center the window on screen if no saved position */
    if (config.window_x == -1 || config.window_y == -1) {
        RECT rc;
        GetWindowRect(hwnd, &rc);
        int win_w = rc.right - rc.left;
        int win_h = rc.bottom - rc.top;

        int screen_w = GetSystemMetrics(SM_CXSCREEN);
        int screen_h = GetSystemMetrics(SM_CYSCREEN);

        SetWindowPos(hwnd, NULL, (screen_w - win_w) / 2, (screen_h - win_h) / 2, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
    }

    ShowWindow(hwnd, cmd_show);
    UpdateWindow(hwnd);

    return hwnd;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, LPWSTR cmd_line, int cmd_show) {
    /* Initialize common controls */
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    /* Enable visual styles */
    SetThemeAppProperties(STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS | STAP_ALLOW_WEBCONTENT);

    /* Load configuration */
    load_config();

    /* Create main window */
    main_window = create_window(instance, cmd_show);
    if (!main_window)
        return 1;

    /* Message loop */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
