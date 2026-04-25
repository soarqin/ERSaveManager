/**
 * @file toolbar.c
 * @brief Implementation of the Praxis top toolbar widget.
 * @details Hosts a backup profile combobox plus six action buttons in a
 *          fixed-height (30px) container child window. The container uses
 *          a custom window class registered on first use; all controls are
 *          children of the container HWND.
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
#define TOOLBAR_HEIGHT       30
#define TOOLBAR_CTRL_HEIGHT  22
#define TOOLBAR_CTRL_Y       4
#define TOOLBAR_LEFT_MARGIN  4
#define TOOLBAR_GAP          4
#define TOOLBAR_GROUP_GAP    8
#define TOOLBAR_BTN_SMALL_W  24
#define TOOLBAR_BTN_LARGE_W  88
#define TOOLBAR_COMBO_MIN_W  120

/*
 * Right-side fixed area (combobox excluded):
 *   gap(4) + add(24) + gap(4) + del(24) + group_gap(8) +
 *   backup_full(88) + gap(4) + backup_slot(88) + gap(4) +
 *   restore(88) + gap(4) + undo(88) + right_margin(4) = 432 px
 */
#define TOOLBAR_RIGHT_FIXED  432

/* Window class name for the toolbar container. */
static const wchar_t *TOOLBAR_CLASS_NAME = L"PraxisToolbar";

/* Track whether the toolbar window class has been registered already. */
static bool g_toolbar_class_registered = false;

struct toolbar_s {
    HWND hwnd;             /* Container child window */
    HWND combo;            /* IDC_PROFILE_COMBO */
    HWND btn_add;          /* IDC_BTN_ADD_BACKUP */
    HWND btn_del;          /* IDC_BTN_DEL_BACKUP */
    HWND btn_backup_full;  /* IDC_BTN_BACKUP_FULL */
    HWND btn_backup_slot;  /* IDC_BTN_BACKUP_SLOT */
    HWND btn_restore;      /* IDC_BTN_RESTORE */
    HWND btn_undo;         /* IDC_BTN_UNDO */
    int height;            /* Fixed: TOOLBAR_HEIGHT */
};

/*
 * Toolbar container window procedure.
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

/* Apply the default GUI font to every control in the toolbar. */
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

    t->height = TOOLBAR_HEIGHT;

    /* Container window. Width starts at a placeholder; toolbar_layout will
     * size it to the actual parent width on first call. */
    t->hwnd = CreateWindowExW(
        0,
        TOOLBAR_CLASS_NAME,
        NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 100, TOOLBAR_HEIGHT,
        parent, (HMENU)(uintptr_t)IDC_TOOLBAR, hinst, NULL);
    if (!t->hwnd) {
        LocalFree(t);
        return NULL;
    }

    /* Backup profile combobox (drop-down list — no free typing). */
    t->combo = CreateWindowExW(
        0,
        WC_COMBOBOXW, NULL,
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        TOOLBAR_LEFT_MARGIN, TOOLBAR_CTRL_Y, 240, 200,
        t->hwnd, (HMENU)(uintptr_t)IDC_PROFILE_COMBO, hinst, NULL);

    /* "+" — add backup profile */
    t->btn_add = CreateWindowExW(
        0,
        L"BUTTON", L"+",
        BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        248, TOOLBAR_CTRL_Y, TOOLBAR_BTN_SMALL_W, TOOLBAR_CTRL_HEIGHT,
        t->hwnd, (HMENU)(uintptr_t)IDC_BTN_ADD_BACKUP, hinst, NULL);

    /* "-" — delete backup profile */
    t->btn_del = CreateWindowExW(
        0,
        L"BUTTON", L"-",
        BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        276, TOOLBAR_CTRL_Y, TOOLBAR_BTN_SMALL_W, TOOLBAR_CTRL_HEIGHT,
        t->hwnd, (HMENU)(uintptr_t)IDC_BTN_DEL_BACKUP, hinst, NULL);

    /* "Backup Full" */
    t->btn_backup_full = CreateWindowExW(
        0,
        L"BUTTON", praxis_locale_str(STR_PRAXIS_TIP_BACKUP_FULL),
        BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        308, TOOLBAR_CTRL_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_CTRL_HEIGHT,
        t->hwnd, (HMENU)(uintptr_t)IDC_BTN_BACKUP_FULL, hinst, NULL);

    /* "Backup Slot" */
    t->btn_backup_slot = CreateWindowExW(
        0,
        L"BUTTON", praxis_locale_str(STR_PRAXIS_TIP_BACKUP_SLOT),
        BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        400, TOOLBAR_CTRL_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_CTRL_HEIGHT,
        t->hwnd, (HMENU)(uintptr_t)IDC_BTN_BACKUP_SLOT, hinst, NULL);

    /* "Restore" */
    t->btn_restore = CreateWindowExW(
        0,
        L"BUTTON", praxis_locale_str(STR_PRAXIS_TIP_RESTORE),
        BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        492, TOOLBAR_CTRL_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_CTRL_HEIGHT,
        t->hwnd, (HMENU)(uintptr_t)IDC_BTN_RESTORE, hinst, NULL);

    /* "Undo Last Restore" */
    t->btn_undo = CreateWindowExW(
        0,
        L"BUTTON", praxis_locale_str(STR_PRAXIS_TIP_UNDO),
        BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        584, TOOLBAR_CTRL_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_CTRL_HEIGHT,
        t->hwnd, (HMENU)(uintptr_t)IDC_BTN_UNDO, hinst, NULL);

    /* Bail out if any control failed to create. The container window will
     * be torn down along with any partially-created children. */
    if (!t->combo || !t->btn_add || !t->btn_del ||
        !t->btn_backup_full || !t->btn_backup_slot ||
        !t->btn_restore || !t->btn_undo) {
        DestroyWindow(t->hwnd);
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
    if (t->hwnd && IsWindow(t->hwnd)) {
        DestroyWindow(t->hwnd);
    }
    LocalFree(t);
}

HWND toolbar_get_hwnd(const toolbar_t *t) {
    return t ? t->hwnd : NULL;
}

int toolbar_get_height(const toolbar_t *t) {
    return t ? t->height : 0;
}

void toolbar_layout(toolbar_t *t, int parent_width) {
    int combo_w;
    int x;

    if (!t || !t->hwnd) {
        return;
    }

    /* Combobox stretches; right-aligned button cluster occupies a fixed
     * footprint (TOOLBAR_RIGHT_FIXED px including the leading gap and the
     * trailing right margin). Clamp to the configured minimum so the combo
     * never collapses to zero width on very small windows. */
    combo_w = parent_width - TOOLBAR_RIGHT_FIXED - TOOLBAR_LEFT_MARGIN;
    if (combo_w < TOOLBAR_COMBO_MIN_W) {
        combo_w = TOOLBAR_COMBO_MIN_W;
    }

    x = TOOLBAR_LEFT_MARGIN;
    MoveWindow(t->combo, x, TOOLBAR_CTRL_Y, combo_w, TOOLBAR_CTRL_HEIGHT, TRUE);
    x += combo_w + TOOLBAR_GAP;

    MoveWindow(t->btn_add, x, TOOLBAR_CTRL_Y, TOOLBAR_BTN_SMALL_W, TOOLBAR_CTRL_HEIGHT, TRUE);
    x += TOOLBAR_BTN_SMALL_W + TOOLBAR_GAP;

    MoveWindow(t->btn_del, x, TOOLBAR_CTRL_Y, TOOLBAR_BTN_SMALL_W, TOOLBAR_CTRL_HEIGHT, TRUE);
    x += TOOLBAR_BTN_SMALL_W + TOOLBAR_GROUP_GAP;

    MoveWindow(t->btn_backup_full, x, TOOLBAR_CTRL_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_CTRL_HEIGHT, TRUE);
    x += TOOLBAR_BTN_LARGE_W + TOOLBAR_GAP;

    MoveWindow(t->btn_backup_slot, x, TOOLBAR_CTRL_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_CTRL_HEIGHT, TRUE);
    x += TOOLBAR_BTN_LARGE_W + TOOLBAR_GAP;

    MoveWindow(t->btn_restore, x, TOOLBAR_CTRL_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_CTRL_HEIGHT, TRUE);
    x += TOOLBAR_BTN_LARGE_W + TOOLBAR_GAP;

    MoveWindow(t->btn_undo, x, TOOLBAR_CTRL_Y, TOOLBAR_BTN_LARGE_W, TOOLBAR_CTRL_HEIGHT, TRUE);

    /* Resize the toolbar container to span the parent width. */
    SetWindowPos(t->hwnd, NULL, 0, 0, parent_width, t->height,
                 SWP_NOMOVE | SWP_NOZORDER);
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
