#include "resource.h"

#include "config.h"
#include "ersave.h"
#include "locale.h"

#include "embedded_face_data.h"
#include "file_dialog.h"

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
HWND button_change_folder, combo_box_save_folder, list_view_faces, list_view_chars;
HWND button_manage_faces;  /* Button to open face management window */
HWND main_window;
HMENU menu_bar = NULL, embedded_face_data_menu = NULL;
HFONT default_font;  /* font handle */
er_save_data_t *save_data = NULL;
HWND label_chars;  /* Label handle for characters ListView */
HWND detail_group;  /* GroupBox for attribute details panel */
HWND detail_stat_labels[STAT_COUNT];  /* Static labels for attribute names */
HWND detail_stat_values[STAT_COUNT];  /* Static labels for attribute values */

/* Mapping from stat index to locale string index */
static const locale_string_index_t stat_str_indices[STAT_COUNT] = {
    STR_VIGOR, STR_MIND, STR_ENDURANCE, STR_STRENGTH,
    STR_DEXTERITY, STR_INTELLIGENCE, STR_FAITH, STR_ARCANE
};

static void update_char_list_view(int item, const er_char_data_t *char_data);
static void set_stat_label_text(int idx);
static void update_detail_panel(int slot);

static void add_folders_to_combo_box(void) {
    /* Clear the combo box */
    SendMessageW(combo_box_save_folder, CB_RESETCONTENT, 0, 0);

    /* Get user's AppData\Roaming path */
    wchar_t search_path[MAX_PATH];
    if (config.save_path[0] == L'\0') return;

    /* Create search pattern for subdirectories */
    lstrcpyW(search_path, config.save_path);
    PathAppendW(search_path, L"\\*");
    WIN32_FIND_DATAW find_data;
    HANDLE find = FindFirstFileW(search_path, &find_data);

    if (find != INVALID_HANDLE_VALUE) {
        do {
            /* Skip "." and ".." directories */
            if (find_data.cFileName[0] == L'.' && (find_data.cFileName[1] == L'\0' || (find_data.cFileName[1] == L'.' && find_data.cFileName[2] == L'\0'))) {
                continue;
            }

            /* Skip non-directory entries */
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                continue;
            }

            /* Check if SteamID is valid */
            wchar_t *endptr;
            uint64_t steam_id = wcstoull(find_data.cFileName, &endptr, 10);
            /* Steam ID = (Universe << 56) | (Type << 52) | (Instance << 32) | AccountID
                *  Universe: 0-3
                *  Type: 1-10
                *  Instance: usually 1
                * So SteamID is always greater than 0x10000000000000ULL */
            if (*endptr != L'\0' || steam_id < 0x10000000000000ULL) {
                continue;
            }

            /* Check if ER0000.sl2 exists in the directory */
            wchar_t save_path[MAX_PATH];
            lstrcpyW(save_path, config.save_path);
            PathAppendW(save_path, find_data.cFileName);
            PathAppendW(save_path, L"\\ER0000.sl2");

            if (!PathFileExistsW(save_path)) {
                continue;
            }

            SendMessageW(combo_box_save_folder, CB_ADDSTRING, 0, (LPARAM)find_data.cFileName);
        } while (FindNextFileW(find, &find_data));

        FindClose(find);
    }
}

static bool handle_save_folder_selection(HWND hwnd) {
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

/* Function to create embedded face data menu */
static void create_embedded_face_data_menu(HWND hwnd) {
    if (embedded_face_data_menu) {
        DestroyMenu(embedded_face_data_menu);
    }
    /* Create embedded face data menu */
    embedded_face_data_menu = CreatePopupMenu();

    HMENU popup_menus[4] = {
        CreatePopupMenu(),
        CreatePopupMenu(),
        CreatePopupMenu(),
        CreatePopupMenu(),
    };

    int locale_idx = get_current_locale();
    /* Add embedded face data menu to menu bar */
    for (int i = 0; i < embedded_face_data_count; i++) {
        AppendMenuW(popup_menus[embedded_face_data[i].category], MF_STRING, IDM_EMBEDDED_FACE_DATA_START + i, embedded_face_data[i].name[locale_idx]);
    }

    AppendMenuW(embedded_face_data_menu, MF_POPUP, (UINT_PTR)popup_menus[0], locale_str(STR_NPC_BASE));
    AppendMenuW(embedded_face_data_menu, MF_POPUP, (UINT_PTR)popup_menus[1], locale_str(STR_NPC_BASE_NON_INTERACTABLE));
    AppendMenuW(embedded_face_data_menu, MF_POPUP, (UINT_PTR)popup_menus[2], locale_str(STR_NPC_DLC));
    AppendMenuW(embedded_face_data_menu, MF_POPUP, (UINT_PTR)popup_menus[3], locale_str(STR_NPC_DLC_NON_INTERACTABLE));
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

    /* Tick menu item*/
    CheckMenuItem(menu_bar, IDM_LOCALE_START + idx, MF_CHECKED);

    /* Save new language to config */
    save_config();

    /* Update UI strings */
    create_embedded_face_data_menu(main_window);

    wchar_t window_title[64];
    wsprintfW(window_title, L"%s v%s", locale_str(STR_APP_TITLE), VERSION_STR);
    SetWindowTextW(main_window, window_title);
    SetWindowTextW(button_change_folder, locale_str(STR_CHANGE_SAVE_FOLDER));
    SetWindowTextW(button_manage_faces, locale_str(STR_MANAGE_FACES));
    SetWindowTextW(label_chars, locale_str(STR_CHARACTERS));

    /* Update detail panel labels */
    SetWindowTextW(detail_group, locale_str(STR_ATTRIBUTES));
    for (int i = 0; i < STAT_COUNT; i++) {
        set_stat_label_text(i);
    }

    /* Update menu title */
    HMENU locale_menu = GetSubMenu(menu_bar, 0);
    ModifyMenuW(menu_bar, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)locale_menu, locale_str(STR_LANGUAGE));
    DrawMenuBar(main_window);

    /* Update characters ListView columns */
    LVCOLUMNW lvc;
    lvc.mask = LVCF_TEXT;

    lvc.iSubItem = 0;
    lvc.pszText = (LPWSTR)locale_str(STR_SLOT);
    ListView_SetColumn(list_view_chars, 0, &lvc);

    lvc.iSubItem = 1;
    lvc.pszText = (LPWSTR)locale_str(STR_NAME);
    ListView_SetColumn(list_view_chars, 1, &lvc);

    lvc.iSubItem = 2;
    lvc.pszText = (LPWSTR)locale_str(STR_BODY_TYPE);
    ListView_SetColumn(list_view_chars, 2, &lvc);

    lvc.iSubItem = 3;
    lvc.pszText = (LPWSTR)locale_str(STR_LEVEL);
    ListView_SetColumn(list_view_chars, 3, &lvc);

    lvc.iSubItem = 4;
    lvc.pszText = (LPWSTR)locale_str(STR_IN_GAME_TIME);
    ListView_SetColumn(list_view_chars, 4, &lvc);

    if (!save_data) {
        return;
    };

    /* Update characters ListView items */
    for (int i = 0; i < 10; i++) {
        const er_char_data_t *char_data = er_char_data_ref(save_data, i);
        update_char_list_view(i, char_data);
    }
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

static void update_char_list_view(int item, const er_char_data_t *char_data) {
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

/* Helper to set stat label text with colon suffix */
static void set_stat_label_text(int idx) {
    wchar_t text[64];
    wsprintfW(text, L"%s:", locale_str(stat_str_indices[idx]));
    SetWindowTextW(detail_stat_labels[idx], text);
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

/* Function to create all controls */
static void create_controls(HWND hwnd, HMODULE module) {
    /* Get system default font */
    NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), &ncm, 0);
    default_font = CreateFontIndirectW(&ncm.lfMessageFont);

    /* Create Button */
    button_change_folder = CreateWindowW(
        L"BUTTON", locale_str(STR_CHANGE_SAVE_FOLDER),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 10, 160, 25,
        hwnd, (HMENU)IDC_BUTTON_CHANGE_FOLDER, module, NULL
    );
    SendMessage(button_change_folder, WM_SETFONT, (WPARAM)default_font, TRUE);

    /* Create ComboBox */
    combo_box_save_folder = CreateWindowW(
        L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        170, 10, 200, 25,
        hwnd, (HMENU)IDC_COMBO_SAVE_FOLDER, module, NULL
    );
    SendMessage(combo_box_save_folder, WM_SETFONT, (WPARAM)default_font, TRUE);

    /* Create Characters Label */
    label_chars = CreateWindowW(
        L"STATIC", locale_str(STR_CHARACTERS),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        10, 45, 200, 20,
        hwnd, (HMENU)6, module, NULL
    );
    SendMessage(label_chars, WM_SETFONT, (WPARAM)default_font, TRUE);

    /* Create Characters ListView */
    list_view_chars = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        10, 65, 200, 280,
        hwnd, (HMENU)4, module, NULL
    );
    ListView_SetExtendedListViewStyleEx(list_view_chars,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_BORDERSELECT,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_BORDERSELECT);
    SendMessage(list_view_chars, WM_SETFONT, (WPARAM)default_font, TRUE);

    /* Add columns to Characters ListView */
    LVCOLUMNW lvc;
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    lvc.iSubItem = 0;
    lvc.cx = 40;
    lvc.pszText = (LPWSTR)locale_str(STR_SLOT);
    ListView_InsertColumn(list_view_chars, 0, &lvc);

    lvc.iSubItem = 1;
    lvc.cx = 120;
    lvc.pszText = (LPWSTR)locale_str(STR_NAME);
    ListView_InsertColumn(list_view_chars, 1, &lvc);

    lvc.iSubItem = 2;
    lvc.cx = 80;
    lvc.pszText = (LPWSTR)locale_str(STR_BODY_TYPE);
    ListView_InsertColumn(list_view_chars, 2, &lvc);

    lvc.iSubItem = 3;
    lvc.cx = 60;
    lvc.pszText = (LPWSTR)locale_str(STR_LEVEL);
    ListView_InsertColumn(list_view_chars, 3, &lvc);

    lvc.iSubItem = 4;
    lvc.cx = 95;
    lvc.pszText = (LPWSTR)locale_str(STR_IN_GAME_TIME);
    ListView_InsertColumn(list_view_chars, 4, &lvc);

    /* Create detail panel group box for attributes */
    detail_group = CreateWindowW(
        L"BUTTON", locale_str(STR_ATTRIBUTES),
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 220, 250,
        hwnd, NULL, module, NULL
    );
    SendMessage(detail_group, WM_SETFONT, (WPARAM)default_font, TRUE);

    /* Create stat label/value pairs inside detail panel */
    for (int i = 0; i < STAT_COUNT; i++) {
        detail_stat_labels[i] = CreateWindowW(
            L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            0, 0, 90, 18,
            hwnd, NULL, module, NULL
        );
        SendMessage(detail_stat_labels[i], WM_SETFONT, (WPARAM)default_font, TRUE);
        set_stat_label_text(i);

        detail_stat_values[i] = CreateWindowW(
            L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 60, 18,
            hwnd, NULL, module, NULL
        );
        SendMessage(detail_stat_values[i], WM_SETFONT, (WPARAM)default_font, TRUE);
    }

    /* Create Manage Faces Button */
    button_manage_faces = CreateWindowW(
        L"BUTTON", locale_str(STR_MANAGE_FACES),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        380, 10, 120, 25,
        hwnd, (HMENU)IDC_BUTTON_MANAGE_FACES, module, NULL
    );
    SendMessage(button_manage_faces, WM_SETFONT, (WPARAM)default_font, TRUE);

    add_folders_to_combo_box();

    /* Set initial ComboBox selection */
    if (config.save_subfolder[0] == L'\0') {
        SendMessageW(combo_box_save_folder, CB_SETCURSEL, 0, 0);
    } else {
        int idx = SendMessageW(combo_box_save_folder, CB_FINDSTRING, -1, (LPARAM)config.save_subfolder);
        SendMessageW(combo_box_save_folder, CB_SETCURSEL, idx == CB_ERR ? 0 : idx, 0);
    }
    handle_save_folder_selection(hwnd);

    /* Create menu bar */
    menu_bar = CreateMenu();
    HMENU locale_menu = CreatePopupMenu();

    /* Add locale options */
    int locale_cnt = locale_count();
    for (int i = 0; i < locale_cnt; i++) {
        AppendMenuW(locale_menu, MF_STRING, IDM_LOCALE_START + i, locale_name(i));
    }

    /* Add locale menu to menu bar */
    AppendMenuW(menu_bar, MF_POPUP, (UINT_PTR)locale_menu, locale_str(STR_LANGUAGE));
    SetMenu(hwnd, menu_bar);

    /* Tick menu item */
    CheckMenuItem(menu_bar, IDM_LOCALE_START + get_current_locale(), MF_CHECKED);

    create_embedded_face_data_menu(hwnd);
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

/* Function to layout controls */
static void layout_controls(HWND hwnd, int width, int height) {
    int btn_face_w = 120;
    int gap = 10;
    int detail_w = 220;
    int detail_x = width - gap - detail_w;
    int list_w = detail_x - gap - gap;
    int content_y = 65;
    int content_h = height - 75;

    /* 5 base controls + group box + 8 label/value pairs = 22 */
    HDWP hdwp = BeginDeferWindowPos(5 + 1 + STAT_COUNT * 2);

    /* Top row */
    hdwp = DeferWindowPos(hdwp, button_change_folder, NULL,
        gap, 10, 160, 25, SWP_NOZORDER);
    hdwp = DeferWindowPos(hdwp, combo_box_save_folder, NULL,
        170, 10, width - 180 - btn_face_w - gap, 25, SWP_NOZORDER);
    hdwp = DeferWindowPos(hdwp, button_manage_faces, NULL,
        width - gap - btn_face_w, 10, btn_face_w, 25, SWP_NOZORDER);

    /* Characters label (above list only) */
    hdwp = DeferWindowPos(hdwp, label_chars, NULL,
        gap, 45, list_w, 20, SWP_NOZORDER);

    /* Characters ListView (left side) */
    hdwp = DeferWindowPos(hdwp, list_view_chars, NULL,
        gap, content_y, list_w, content_h, SWP_NOZORDER);

    /* Detail panel group box (right side) */
    hdwp = DeferWindowPos(hdwp, detail_group, NULL,
        detail_x, 45, detail_w, content_h + 20, SWP_NOZORDER);

    /* Stat label/value rows inside the detail panel area */
    int row_h = 24;
    int label_x = detail_x + 12;
    int label_w = 100;
    int value_x = detail_x + 118;
    int value_w = detail_w - 130;
    int first_row_y = content_y + 5;

    for (int i = 0; i < STAT_COUNT; i++) {
        int row_y = first_row_y + i * row_h;
        hdwp = DeferWindowPos(hdwp, detail_stat_labels[i], NULL,
            label_x, row_y, label_w, 18, SWP_NOZORDER);
        hdwp = DeferWindowPos(hdwp, detail_stat_values[i], NULL,
            value_x, row_y, value_w, 18, SWP_NOZORDER);
    }

    /* Apply all window position changes at once */
    EndDeferWindowPos(hdwp);
}

/* Function to handle window resize */
static void on_window_resize(HWND hwnd, WPARAM wparam, LPARAM lparam) {
    /* Get window dimensions */
    int width = LOWORD(lparam);
    int height = HIWORD(lparam);

    layout_controls(hwnd, width, height);
}

/* Window procedure */
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE: {
            HMODULE module = GetModuleHandle(NULL);

            /* Create all controls */
            create_controls(hwnd, module);

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
            DeleteObject(default_font);  /* Clean up the font */
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
