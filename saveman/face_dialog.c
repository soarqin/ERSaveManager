/**
 * @file face_dialog.c
 * @brief Face data management dialog implementation
 * @details Implements the face data management modal dialog, including
 *          the ListView for face slots, context menu, and import/export operations.
 */
#include "face_dialog.h"
#include "ersave.h"
#include "locale.h"
#include "embedded_face_data.h"
#include "file_dialog.h"
#include "resource.h"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

/* Globals declared in main.c */
extern er_save_data_t *save_data;
extern HFONT default_font;
extern HMENU embedded_face_data_menu;

/* ListView handle for the face data dialog — local to this module */
static HWND list_view_faces = NULL;

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
LRESULT CALLBACK face_data_dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
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
