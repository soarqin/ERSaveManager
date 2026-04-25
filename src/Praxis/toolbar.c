/**
 * @file toolbar.c
 * @brief Implementation of the Praxis two-row toolbar widget.
 * @details Hosts a backup profile combobox plus add/delete buttons in a
 *          fixed-height (30 px) top container, and four wide action buttons
 *          (Backup Full, Backup Slot, Restore, Undo) in a 38 px bottom
 *          container. Both containers use the same custom window class which
 *          forwards WM_COMMAND/WM_NOTIFY from child controls up to the parent
 *          window.
 */

#include "toolbar.h"
#include "resource.h"
#include "locale.h"

#include <stdbool.h>
#include <stdio.h>
#include <wchar.h>

#include <windows.h>
#include <commctrl.h>

/* Toolbar layout constants */
#define TOOLBAR_TOP_HEIGHT       30
#define TOOLBAR_BOTTOM_HEIGHT    38     /* slightly taller to accommodate larger buttons */
#define TOOLBAR_CTRL_HEIGHT      22
#define TOOLBAR_BTN_LARGE_HEIGHT 28     /* taller buttons in bottom toolbar */
#define TOOLBAR_CTRL_Y           4
#define TOOLBAR_BTN_LARGE_Y      5      /* center 28px button in 38px container */
#define TOOLBAR_LEFT_MARGIN      4
#define TOOLBAR_RIGHT_MARGIN     4
#define TOOLBAR_GAP              4
#define TOOLBAR_GROUP_GAP        8
#define TOOLBAR_BTN_SMALL_W      24
#define TOOLBAR_BTN_LARGE_W      140    /* WIDER — fits "Backup Slot", "Undo Restore" with margin */
#define TOOLBAR_COMBO_MIN_W      120

/* Window class name shared by both top and bottom toolbar containers. */
static const wchar_t *TOOLBAR_CLASS_NAME = L"PraxisToolbar";

/* Track whether the toolbar window class has been registered already. */
static bool g_toolbar_class_registered = false;

struct toolbar_s {
    HWND hwnd_top;          /* Top container child window */
    HWND hwnd_bottom;       /* Bottom container child window */
    HWND combo;             /* IDC_PROFILE_COMBO       — child of hwnd_top */
    HWND btn_add;           /* IDC_BTN_ADD_BACKUP      — child of hwnd_top */
    HWND btn_del;           /* IDC_BTN_DEL_BACKUP      — child of hwnd_top */
    HWND btn_backup_full;   /* IDC_BTN_BACKUP_FULL     — child of hwnd_bottom */
    HWND btn_backup_slot;   /* IDC_BTN_BACKUP_SLOT     — child of hwnd_bottom */
    HWND btn_restore;       /* IDC_BTN_RESTORE         — child of hwnd_bottom */
    HWND btn_undo;          /* IDC_BTN_UNDO            — child of hwnd_bottom */
    int top_height;         /* Fixed: TOOLBAR_TOP_HEIGHT */
    int bottom_height;      /* Fixed: TOOLBAR_BOTTOM_HEIGHT */
};

/*
 * Toolbar container window procedure (shared by top and bottom containers).
 *
 * Forwards WM_COMMAND and WM_NOTIFY from the toolbar's child controls up to
 * the parent window so the main wnd proc can handle button clicks and
 * combobox selection-change notifications. All other messages fall through
 * to DefWindowProcW.
 */
static LRESULT CALLBACK toolbar_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND || msg == WM_NOTIFY) {
        HWND parent = GetParent(hwnd);
        if (parent) {
            return SendMessageW(parent, msg, wp, lp);
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* Register the toolbar container window class once per process. */
static void toolbar_register_class(HINSTANCE hinst) {
    WNDCLASSEXW wc;

    if (g_toolbar_class_registered) {
        return;
    }

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = toolbar_wnd_proc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = TOOLBAR_CLASS_NAME;

    /* Ignore failure when the class is already registered (e.g. multiple
     * toolbar instances or hot-reload during selftest). */
    RegisterClassExW(&wc);
    g_toolbar_class_registered = true;
}

/* Apply the default GUI font to every control across both containers. */
static void toolbar_apply_default_font(const struct toolbar_s *t) {
    HFONT hfont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    SendMessageW(t->combo,           WM_SETFONT, (WPARAM)hfont, FALSE);
    SendMessageW(t->btn_add,         WM_SETFONT, (WPARAM)hfont, FALSE);
    SendMessageW(t->btn_del,         WM_SETFONT, (WPARAM)hfont, FALSE);
    SendMessageW(t->btn_backup_full, WM_SETFONT, (WPARAM)hfont, FALSE);
    SendMessageW(t->btn_backup_slot, WM_SETFONT, (WPARAM)hfont, FALSE);
    SendMessageW(t->btn_restore,     WM_SETFONT, (WPARAM)hfont, FALSE);
    SendMessageW(t->btn_undo,        WM_SETFONT, (WPARAM)hfont, FALSE);
}

toolbar_t *toolbar_create(HWND parent, HINSTANCE hinst) {
    struct toolbar_s *t;

    if (!parent || !hinst) {
        return NULL;
    }

    toolbar_register_class(hinst);

    t = (struct toolbar_s *)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(*t));
    if (!t) {
        return NULL;
    }

    t->top_height    = TOOLBAR_TOP_HEIGHT;
    t->bottom_height = TOOLBAR_BOTTOM_HEIGHT;

    /* Top container: combo + "+" + "-". Width starts at a placeholder;
     * toolbar_layout_top will size it to the actual parent width on first
     * call. */
    t->hwnd_top = CreateWindowExW(
        0,
        TOOLBAR_CLASS_NAME,
        NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 100, t->top_height,
        parent, (HMENU)(uintptr_t)IDC_TOOLBAR, hinst, NULL);

    /* Bottom container: 4 action buttons. No control id needed; identified
     * by HWND from its child controls' IDs. */
    t->hwnd_bottom = CreateWindowExW(
        0,
        TOOLBAR_CLASS_NAME,
        NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 100, t->bottom_height,
        parent, NULL, hinst, NULL);

    if (!t->hwnd_top || !t->hwnd_bottom) {
        if (t->hwnd_top)    DestroyWindow(t->hwnd_top);
        if (t->hwnd_bottom) DestroyWindow(t->hwnd_bottom);
        LocalFree(t);
        return NULL;
    }

    /* --- Top container children: backup profile combobox + add/del --- */

    /* Backup profile combobox (drop-down list — no free typing). */
    t->combo = CreateWindowExW(
        0,
        WC_COMBOBOXW, NULL,
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        TOOLBAR_LEFT_MARGIN, TOOLBAR_CTRL_Y, 240, 200,
        t->hwnd_top, (HMENU)(uintptr_t)IDC_PROFILE_COMBO, hinst, NULL);

    /* "+" — add backup profile */
    t->btn_add = CreateWindowExW(
        0,
        L"BUTTON", L"+",
        BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        248, TOOLBAR_CTRL_Y, TOOLBAR_BTN_SMALL_W, TOOLBAR_CTRL_HEIGHT,
        t->hwnd_top, (HMENU)(uintptr_t)IDC_BTN_ADD_BACKUP, hinst, NULL);

    /* "-" — delete backup profile */
    t->btn_del = CreateWindowExW(
        0,
        L"BUTTON", L"-",
        BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        276, TOOLBAR_CTRL_Y, TOOLBAR_BTN_SMALL_W, TOOLBAR_CTRL_HEIGHT,
        t->hwnd_top, (HMENU)(uintptr_t)IDC_BTN_DEL_BACKUP, hinst, NULL);

    /* --- Bottom container children: 4 wide action buttons --- */

    /* "Backup Full" */
    t->btn_backup_full = CreateWindowExW(
        0,
        L"BUTTON", praxis_locale_str(STR_PRAXIS_TIP_BACKUP_FULL),
        BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        TOOLBAR_LEFT_MARGIN, TOOLBAR_BTN_LARGE_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_BTN_LARGE_HEIGHT,
        t->hwnd_bottom, (HMENU)(uintptr_t)IDC_BTN_BACKUP_FULL, hinst, NULL);

    /* "Backup Slot" */
    t->btn_backup_slot = CreateWindowExW(
        0,
        L"BUTTON", praxis_locale_str(STR_PRAXIS_TIP_BACKUP_SLOT),
        BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        148, TOOLBAR_BTN_LARGE_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_BTN_LARGE_HEIGHT,
        t->hwnd_bottom, (HMENU)(uintptr_t)IDC_BTN_BACKUP_SLOT, hinst, NULL);

    /* "Restore" */
    t->btn_restore = CreateWindowExW(
        0,
        L"BUTTON", praxis_locale_str(STR_PRAXIS_TIP_RESTORE),
        BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        292, TOOLBAR_BTN_LARGE_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_BTN_LARGE_HEIGHT,
        t->hwnd_bottom, (HMENU)(uintptr_t)IDC_BTN_RESTORE, hinst, NULL);

    /* "Undo Last Restore" */
    t->btn_undo = CreateWindowExW(
        0,
        L"BUTTON", praxis_locale_str(STR_PRAXIS_TIP_UNDO),
        BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        436, TOOLBAR_BTN_LARGE_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_BTN_LARGE_HEIGHT,
        t->hwnd_bottom, (HMENU)(uintptr_t)IDC_BTN_UNDO, hinst, NULL);

    /* Bail out if any control failed to create. Both container windows will
     * be torn down along with any partially-created children. */
    if (!t->combo || !t->btn_add || !t->btn_del ||
        !t->btn_backup_full || !t->btn_backup_slot ||
        !t->btn_restore || !t->btn_undo) {
        DestroyWindow(t->hwnd_top);
        DestroyWindow(t->hwnd_bottom);
        LocalFree(t);
        return NULL;
    }

    toolbar_apply_default_font(t);

    return t;
}

void toolbar_destroy(toolbar_t *t) {
    if (!t) {
        return;
    }
    if (t->hwnd_top && IsWindow(t->hwnd_top)) {
        DestroyWindow(t->hwnd_top);
    }
    if (t->hwnd_bottom && IsWindow(t->hwnd_bottom)) {
        DestroyWindow(t->hwnd_bottom);
    }
    LocalFree(t);
}

HWND toolbar_get_hwnd_top(const toolbar_t *t) {
    return t ? t->hwnd_top : NULL;
}

HWND toolbar_get_hwnd_bottom(const toolbar_t *t) {
    return t ? t->hwnd_bottom : NULL;
}

int toolbar_get_top_height(const toolbar_t *t) {
    return t ? t->top_height : 0;
}

int toolbar_get_bottom_height(const toolbar_t *t) {
    return t ? t->bottom_height : 0;
}

void toolbar_layout_top(toolbar_t *t, int parent_width) {
    int right_fixed;
    int combo_w;
    int x;

    if (!t || !t->hwnd_top) {
        return;
    }

    /* Combo stretches; "+" / "-" stay on right of top toolbar. The fixed
     * right-side footprint is two small buttons separated by gaps plus the
     * trailing right margin. */
    right_fixed = TOOLBAR_BTN_SMALL_W * 2 + TOOLBAR_GAP * 2 + TOOLBAR_RIGHT_MARGIN;
    combo_w = parent_width - right_fixed - TOOLBAR_LEFT_MARGIN;
    if (combo_w < TOOLBAR_COMBO_MIN_W) {
        combo_w = TOOLBAR_COMBO_MIN_W;
    }

    x = TOOLBAR_LEFT_MARGIN;
    MoveWindow(t->combo, x, TOOLBAR_CTRL_Y, combo_w, TOOLBAR_CTRL_HEIGHT, TRUE);
    x += combo_w + TOOLBAR_GAP;

    MoveWindow(t->btn_add, x, TOOLBAR_CTRL_Y, TOOLBAR_BTN_SMALL_W, TOOLBAR_CTRL_HEIGHT, TRUE);
    x += TOOLBAR_BTN_SMALL_W + TOOLBAR_GAP;

    MoveWindow(t->btn_del, x, TOOLBAR_CTRL_Y, TOOLBAR_BTN_SMALL_W, TOOLBAR_CTRL_HEIGHT, TRUE);

    /* Resize the top toolbar container to span the parent width (anchored
     * at y=0). */
    SetWindowPos(t->hwnd_top, NULL, 0, 0, parent_width, t->top_height,
                 SWP_NOMOVE | SWP_NOZORDER);
}

void toolbar_layout_bottom(toolbar_t *t, int parent_width, int y_top) {
    int x;

    if (!t || !t->hwnd_bottom) {
        return;
    }

    /* All 4 action buttons left-aligned with equal width
     * (TOOLBAR_BTN_LARGE_W = 140 px) so even long localized labels such as
     * "Backup Full Save" / "Undo Last Restore" fit without clipping. */
    x = TOOLBAR_LEFT_MARGIN;
    MoveWindow(t->btn_backup_full, x, TOOLBAR_BTN_LARGE_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_BTN_LARGE_HEIGHT, TRUE);
    x += TOOLBAR_BTN_LARGE_W + TOOLBAR_GAP;

    MoveWindow(t->btn_backup_slot, x, TOOLBAR_BTN_LARGE_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_BTN_LARGE_HEIGHT, TRUE);
    x += TOOLBAR_BTN_LARGE_W + TOOLBAR_GAP;

    MoveWindow(t->btn_restore, x, TOOLBAR_BTN_LARGE_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_BTN_LARGE_HEIGHT, TRUE);
    x += TOOLBAR_BTN_LARGE_W + TOOLBAR_GAP;

    MoveWindow(t->btn_undo, x, TOOLBAR_BTN_LARGE_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_BTN_LARGE_HEIGHT, TRUE);

    /* Reposition + resize the bottom container to (0, y_top) spanning the
     * parent width. */
    SetWindowPos(t->hwnd_bottom, NULL, 0, y_top, parent_width, t->bottom_height,
                 SWP_NOZORDER);
}

void toolbar_populate_profiles(toolbar_t *t, const profile_store_t *store) {
    size_t i;

    if (!t || !t->combo) {
        return;
    }

    SendMessageW(t->combo, CB_RESETCONTENT, 0, 0);

    if (!store) {
        return;
    }

    for (i = 0; i < store->backup_count; i++) {
        const backup_profile_t *bp = &store->backups[i];
        const wchar_t *game_name = L"?";
        wchar_t label[160];
        size_t g;
        LRESULT idx;

        /* Resolve parent game name for the "<game> / <backup>" label. */
        for (g = 0; g < store->game_count; g++) {
            if (store->games[g].id == bp->parent_game_id) {
                game_name = store->games[g].name;
                break;
            }
        }

        _snwprintf_s(label, 160, _TRUNCATE, L"%ls / %ls", game_name, bp->name);

        idx = SendMessageW(t->combo, CB_ADDSTRING, 0, (LPARAM)label);
        if (idx != CB_ERR && idx != CB_ERRSPACE) {
            SendMessageW(t->combo, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)bp->id);
        }
    }
}

int toolbar_get_selected_backup_id(const toolbar_t *t) {
    LRESULT sel;
    LRESULT data;

    if (!t || !t->combo) {
        return 0;
    }

    sel = SendMessageW(t->combo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) {
        return 0;
    }

    data = SendMessageW(t->combo, CB_GETITEMDATA, (WPARAM)sel, 0);
    if (data == CB_ERR) {
        return 0;
    }

    return (int)data;
}

void toolbar_set_selected_backup_id(toolbar_t *t, int backup_id) {
    LRESULT count;
    LRESULT i;

    if (!t || !t->combo) {
        return;
    }

    count = SendMessageW(t->combo, CB_GETCOUNT, 0, 0);
    for (i = 0; i < count; i++) {
        LRESULT data = SendMessageW(t->combo, CB_GETITEMDATA, (WPARAM)i, 0);
        if (data != CB_ERR && (int)data == backup_id) {
            SendMessageW(t->combo, CB_SETCURSEL, (WPARAM)i, 0);
            return;
        }
    }

    /* No matching item — clear selection. */
    SendMessageW(t->combo, CB_SETCURSEL, (WPARAM)-1, 0);
}

void toolbar_set_actions_enabled(toolbar_t *t, bool enabled) {
    BOOL flag;

    if (!t) {
        return;
    }

    flag = enabled ? TRUE : FALSE;
    EnableWindow(t->btn_del,         flag);
    EnableWindow(t->btn_backup_full, flag);
    EnableWindow(t->btn_backup_slot, flag);
    EnableWindow(t->btn_restore,     flag);
    EnableWindow(t->btn_undo,        flag);

    /* Combobox and "+" stay enabled so users can always create a profile. */
    EnableWindow(t->combo,   TRUE);
    EnableWindow(t->btn_add, TRUE);
}

void toolbar_apply_locale_strings(toolbar_t *t) {
    if (!t) {
        return;
    }

    /* Re-pull every localized button caption from the active locale. The
     * "+" / "-" buttons are universal symbols and stay as-is. */
    if (t->btn_backup_full) {
        SetWindowTextW(t->btn_backup_full, praxis_locale_str(STR_PRAXIS_TIP_BACKUP_FULL));
    }
    if (t->btn_backup_slot) {
        SetWindowTextW(t->btn_backup_slot, praxis_locale_str(STR_PRAXIS_TIP_BACKUP_SLOT));
    }
    if (t->btn_restore) {
        SetWindowTextW(t->btn_restore, praxis_locale_str(STR_PRAXIS_TIP_RESTORE));
    }
    if (t->btn_undo) {
        SetWindowTextW(t->btn_undo, praxis_locale_str(STR_PRAXIS_TIP_UNDO));
    }
}
