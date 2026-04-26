/**
 * @file hotkey_settings.c
 * @brief Implementation of the Hotkey Settings modal dialog.
 * @details Wraps msctls_hotkey32 (HOTKEY common control) instances — one per
 *          global action — and translates between the control's
 *          packed wParam encoding (HOTKEYF_* | vk) and the project's
 *          hotkey_binding_t (MOD_* | vk) on dialog entry / commit.
 */

#include "hotkey_settings.h"

#include "../config.h"
#include "../hotkey.h"
#include "../locale.h"
#include "../resource.h"
#include "../theme.h"
#include "../../common/theme_core.h"

#include <commctrl.h>
#include <stdbool.h>
#include <wchar.h>
#include <windows.h>

extern bool save_profile_store(void);

/* Default bindings restored by the "Reset Defaults" button. Match the
 * defaults applied in praxis_load_config()::apply_defaults(). */
static const wchar_t *HK_DEFAULT_BACKUP_FULL = L"Ctrl+Shift+F5";
static const wchar_t *HK_DEFAULT_BACKUP_SLOT = L"Ctrl+Shift+F6";
static const wchar_t *HK_DEFAULT_RESTORE     = L"Ctrl+Shift+F9";
static const wchar_t *HK_DEFAULT_UNDO        = L"Ctrl+Shift+Z";
static const wchar_t *HK_DEFAULT_BACKUP_REPLACE = L"Ctrl+Shift+F7";
static const wchar_t *HK_DEFAULT_PREVIOUS_SAVE  = L"Ctrl+Shift+Up";
static const wchar_t *HK_DEFAULT_NEXT_SAVE      = L"Ctrl+Shift+Down";

/* Translate hotkey_binding_t modifier flags (MOD_*) to the HOTKEYF_*
 * bit format used by msctls_hotkey32's HKM_SETHOTKEY message. */
static WORD mod_to_hotkeyf(UINT mod) {
    WORD f = 0;
    if (mod & MOD_SHIFT)   f |= HOTKEYF_SHIFT;
    if (mod & MOD_CONTROL) f |= HOTKEYF_CONTROL;
    if (mod & MOD_ALT)     f |= HOTKEYF_ALT;
    /* HOTKEYF_EXT exists for extended keys; MOD_WIN has no HOTKEY analogue.
     * The HOTKEY control cannot represent the Windows key, so MOD_WIN is
     * silently dropped on display. Users keep MOD_WIN bindings only by
     * editing the INI by hand. */
    return f;
}

/* Inverse of mod_to_hotkeyf: HOTKEYF_* -> MOD_*. */
static UINT hotkeyf_to_mod(WORD f) {
    UINT m = 0;
    if (f & HOTKEYF_SHIFT)   m |= MOD_SHIFT;
    if (f & HOTKEYF_CONTROL) m |= MOD_CONTROL;
    if (f & HOTKEYF_ALT)     m |= MOD_ALT;
    return m;
}

/* Push a hotkey_binding_t into the dialog's HOTKEY control. */
static void hk_set_control_from_binding(HWND dlg, int ctrl_id,
    const hotkey_binding_t *b) {
    WORD lo;
    WORD hi;

    if (!b || b->vk == 0) {
        /* Empty / unparseable string -> blank the control. */
        SendDlgItemMessageW(dlg, ctrl_id, HKM_SETHOTKEY, 0, 0);
        return;
    }

    lo = (WORD)(b->vk & 0xFF);
    hi = mod_to_hotkeyf(b->modifiers);
    SendDlgItemMessageW(dlg, ctrl_id, HKM_SETHOTKEY, MAKEWORD(lo, hi), 0);
}

/* Read the dialog's HOTKEY control into a hotkey_binding_t. Returns false
 * if the control is empty (vk == 0). */
static bool hk_get_binding_from_control(HWND dlg, int ctrl_id,
    hotkey_binding_t *out) {
    WORD packed;
    UINT vk;
    WORD f;

    if (!out) {
        return false;
    }

    packed = (WORD)SendDlgItemMessageW(dlg, ctrl_id, HKM_GETHOTKEY, 0, 0);
    vk = (UINT)LOBYTE(packed);
    f  = (WORD)HIBYTE(packed);

    if (vk == 0) {
        out->modifiers = 0;
        out->vk = 0;
        return false;
    }

    out->vk = vk;
    out->modifiers = hotkeyf_to_mod(f);
    return true;
}

/* Convenience: parse a binding string and push to a control. */
static void hk_set_control_from_string(HWND dlg, int ctrl_id, const wchar_t *s) {
    hotkey_binding_t b = {0};

    if (s && hotkey_parse_string(s, &b)) {
        hk_set_control_from_binding(dlg, ctrl_id, &b);
    } else {
        SendDlgItemMessageW(dlg, ctrl_id, HKM_SETHOTKEY, 0, 0);
    }
}

/* Format a binding into a human-readable string for praxis_config storage. */
static void hk_binding_to_config_string(const hotkey_binding_t *b,
    wchar_t *out, size_t out_chars) {
    if (!out || out_chars == 0) {
        return;
    }
    if (!b || b->vk == 0) {
        out[0] = L'\0';
        return;
    }
    if (!hotkey_to_string(b, out, out_chars)) {
        out[0] = L'\0';
    }
}

/* Re-register all global hotkeys from praxis_config. Returns true if
 * every parseable binding registered without conflict. */
static bool hk_reregister_all(void) {
    hotkey_binding_t b;
    bool ok = true;

    hotkey_unregister_all();

    if (hotkey_parse_string(praxis_config.hotkey_backup_full, &b)) {
        ok &= hotkey_register(HOTKEY_BACKUP_FULL, &b);
    }
    if (hotkey_parse_string(praxis_config.hotkey_backup_slot, &b)) {
        ok &= hotkey_register(HOTKEY_BACKUP_SLOT, &b);
    }
    if (hotkey_parse_string(praxis_config.hotkey_restore, &b)) {
        ok &= hotkey_register(HOTKEY_RESTORE, &b);
    }
    if (hotkey_parse_string(praxis_config.hotkey_undo_restore, &b)) {
        ok &= hotkey_register(HOTKEY_UNDO_RESTORE, &b);
    }
    if (hotkey_parse_string(praxis_config.hotkey_backup_replace, &b)) {
        ok &= hotkey_register(HOTKEY_BACKUP_REPLACE, &b);
    }
    if (hotkey_parse_string(praxis_config.hotkey_previous_save, &b)) {
        ok &= hotkey_register(HOTKEY_PREVIOUS_SAVE, &b);
    }
    if (hotkey_parse_string(praxis_config.hotkey_next_save, &b)) {
        ok &= hotkey_register(HOTKEY_NEXT_SAVE, &b);
    }

    return ok;
}

/* Read all controls and write them into praxis_config. */
static void hk_commit_to_config(HWND dlg) {
    hotkey_binding_t b;

    if (hk_get_binding_from_control(dlg, IDC_HK_BACKUP_FULL, &b)) {
        hk_binding_to_config_string(&b, praxis_config.hotkey_backup_full,
            sizeof(praxis_config.hotkey_backup_full) / sizeof(wchar_t));
    } else {
        praxis_config.hotkey_backup_full[0] = L'\0';
    }
    if (hk_get_binding_from_control(dlg, IDC_HK_BACKUP_SLOT, &b)) {
        hk_binding_to_config_string(&b, praxis_config.hotkey_backup_slot,
            sizeof(praxis_config.hotkey_backup_slot) / sizeof(wchar_t));
    } else {
        praxis_config.hotkey_backup_slot[0] = L'\0';
    }
    if (hk_get_binding_from_control(dlg, IDC_HK_RESTORE, &b)) {
        hk_binding_to_config_string(&b, praxis_config.hotkey_restore,
            sizeof(praxis_config.hotkey_restore) / sizeof(wchar_t));
    } else {
        praxis_config.hotkey_restore[0] = L'\0';
    }
    if (hk_get_binding_from_control(dlg, IDC_HK_UNDO, &b)) {
        hk_binding_to_config_string(&b, praxis_config.hotkey_undo_restore,
            sizeof(praxis_config.hotkey_undo_restore) / sizeof(wchar_t));
    } else {
        praxis_config.hotkey_undo_restore[0] = L'\0';
    }
    if (hk_get_binding_from_control(dlg, IDC_HK_BACKUP_REPLACE, &b)) {
        hk_binding_to_config_string(&b, praxis_config.hotkey_backup_replace,
            sizeof(praxis_config.hotkey_backup_replace) / sizeof(wchar_t));
    } else {
        praxis_config.hotkey_backup_replace[0] = L'\0';
    }
    if (hk_get_binding_from_control(dlg, IDC_HK_PREVIOUS_SAVE, &b)) {
        hk_binding_to_config_string(&b, praxis_config.hotkey_previous_save,
            sizeof(praxis_config.hotkey_previous_save) / sizeof(wchar_t));
    } else {
        praxis_config.hotkey_previous_save[0] = L'\0';
    }
    if (hk_get_binding_from_control(dlg, IDC_HK_NEXT_SAVE, &b)) {
        hk_binding_to_config_string(&b, praxis_config.hotkey_next_save,
            sizeof(praxis_config.hotkey_next_save) / sizeof(wchar_t));
    } else {
        praxis_config.hotkey_next_save[0] = L'\0';
    }
}

static INT_PTR CALLBACK hotkey_settings_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    /* Theme: paint dialog body and child controls in dark colors. */
    case WM_ERASEBKGND:
        if (theme_core_on_erasebkgnd(hwnd, (HDC)wp)) {
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, 1);
            return TRUE;
        }
        return FALSE;
    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        INT_PTR br = theme_core_dlg_ctlcolor((HDC)wp, msg);
        if (br) {
            return br;
        }
        return FALSE;
    }

    case WM_INITDIALOG:
        SetWindowTextW(hwnd, praxis_locale_str(STR_PRAXIS_HOTKEY_SETTINGS));
        SetDlgItemTextW(hwnd, IDC_HK_LBL_BACKUP_FULL,
            praxis_locale_str(STR_PRAXIS_BACKUP_FULL));
        SetDlgItemTextW(hwnd, IDC_HK_LBL_BACKUP_SLOT,
            praxis_locale_str(STR_PRAXIS_BACKUP_SLOT));
        SetDlgItemTextW(hwnd, IDC_HK_LBL_RESTORE,
            praxis_locale_str(STR_PRAXIS_RESTORE));
        SetDlgItemTextW(hwnd, IDC_HK_LBL_UNDO,
            praxis_locale_str(STR_PRAXIS_UNDO_RESTORE));
        SetDlgItemTextW(hwnd, IDC_HK_LBL_BACKUP_REPLACE,
            praxis_locale_str(STR_PRAXIS_BACKUP_REPLACE));
        SetDlgItemTextW(hwnd, IDC_HK_LBL_PREVIOUS_SAVE,
            praxis_locale_str(STR_PRAXIS_PREVIOUS_SAVE));
        SetDlgItemTextW(hwnd, IDC_HK_LBL_NEXT_SAVE,
            praxis_locale_str(STR_PRAXIS_NEXT_SAVE));
        SetDlgItemTextW(hwnd, IDOK,     praxis_locale_str(STR_PRAXIS_BTN_OK));
        SetDlgItemTextW(hwnd, IDCANCEL, praxis_locale_str(STR_PRAXIS_BTN_CANCEL));
        SetDlgItemTextW(hwnd, IDC_HK_RESET, praxis_locale_str(STR_PRAXIS_BTN_RESET_DEFAULTS));

        /* Restrict the HOTKEY controls to combinations that include at least
         * one modifier — global hotkeys without a modifier are unusable in
         * practice (a stray F5 press would fire while typing). */
        SendDlgItemMessageW(hwnd, IDC_HK_BACKUP_FULL, HKM_SETRULES,
            (WPARAM)(HKCOMB_NONE | HKCOMB_S | HKCOMB_A),
            MAKELPARAM(HOTKEYF_CONTROL, 0));
        SendDlgItemMessageW(hwnd, IDC_HK_BACKUP_SLOT, HKM_SETRULES,
            (WPARAM)(HKCOMB_NONE | HKCOMB_S | HKCOMB_A),
            MAKELPARAM(HOTKEYF_CONTROL, 0));
        SendDlgItemMessageW(hwnd, IDC_HK_RESTORE, HKM_SETRULES,
            (WPARAM)(HKCOMB_NONE | HKCOMB_S | HKCOMB_A),
            MAKELPARAM(HOTKEYF_CONTROL, 0));
        SendDlgItemMessageW(hwnd, IDC_HK_UNDO, HKM_SETRULES,
            (WPARAM)(HKCOMB_NONE | HKCOMB_S | HKCOMB_A),
            MAKELPARAM(HOTKEYF_CONTROL, 0));
        SendDlgItemMessageW(hwnd, IDC_HK_BACKUP_REPLACE, HKM_SETRULES,
            (WPARAM)(HKCOMB_NONE | HKCOMB_S | HKCOMB_A),
            MAKELPARAM(HOTKEYF_CONTROL, 0));
        SendDlgItemMessageW(hwnd, IDC_HK_PREVIOUS_SAVE, HKM_SETRULES,
            (WPARAM)(HKCOMB_NONE | HKCOMB_S | HKCOMB_A),
            MAKELPARAM(HOTKEYF_CONTROL, 0));
        SendDlgItemMessageW(hwnd, IDC_HK_NEXT_SAVE, HKM_SETRULES,
            (WPARAM)(HKCOMB_NONE | HKCOMB_S | HKCOMB_A),
            MAKELPARAM(HOTKEYF_CONTROL, 0));

        hk_set_control_from_string(hwnd, IDC_HK_BACKUP_FULL, praxis_config.hotkey_backup_full);
        hk_set_control_from_string(hwnd, IDC_HK_BACKUP_SLOT, praxis_config.hotkey_backup_slot);
        hk_set_control_from_string(hwnd, IDC_HK_RESTORE,     praxis_config.hotkey_restore);
        hk_set_control_from_string(hwnd, IDC_HK_UNDO,        praxis_config.hotkey_undo_restore);
        hk_set_control_from_string(hwnd, IDC_HK_BACKUP_REPLACE, praxis_config.hotkey_backup_replace);
        hk_set_control_from_string(hwnd, IDC_HK_PREVIOUS_SAVE,  praxis_config.hotkey_previous_save);
        hk_set_control_from_string(hwnd, IDC_HK_NEXT_SAVE,      praxis_config.hotkey_next_save);
        praxis_theme_apply_to_window(hwnd);
        return TRUE;

    case WM_SETTINGCHANGE:
        if (theme_core_on_setting_change(wp, lp)) {
            praxis_theme_apply_to_window(hwnd);
        }
        return FALSE;

    case WM_SYSCOLORCHANGE:
        if (theme_core_on_syscolor_change()) {
            praxis_theme_apply_to_window(hwnd);
        }
        return FALSE;

    case WM_THEMECHANGED:
        praxis_theme_apply_to_window(hwnd);
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_HK_RESET:
            hk_set_control_from_string(hwnd, IDC_HK_BACKUP_FULL, HK_DEFAULT_BACKUP_FULL);
            hk_set_control_from_string(hwnd, IDC_HK_BACKUP_SLOT, HK_DEFAULT_BACKUP_SLOT);
            hk_set_control_from_string(hwnd, IDC_HK_RESTORE,     HK_DEFAULT_RESTORE);
            hk_set_control_from_string(hwnd, IDC_HK_UNDO,        HK_DEFAULT_UNDO);
            hk_set_control_from_string(hwnd, IDC_HK_BACKUP_REPLACE, HK_DEFAULT_BACKUP_REPLACE);
            hk_set_control_from_string(hwnd, IDC_HK_PREVIOUS_SAVE,  HK_DEFAULT_PREVIOUS_SAVE);
            hk_set_control_from_string(hwnd, IDC_HK_NEXT_SAVE,      HK_DEFAULT_NEXT_SAVE);
            return TRUE;

        case IDOK:
            /* Read controls -> praxis_config BEFORE EndDialog destroys the
             * dialog. The caller of dialog_hotkey_settings_show() then persists
             * the config and re-registers global hotkeys. */
            hk_commit_to_config(hwnd);
            EndDialog(hwnd, IDOK);
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

bool dialog_hotkey_settings_show(HWND parent) {
    INT_PTR rc;
    bool registered_ok;

    /* Temporarily release all global hotkeys so the user can rebind them to
     * keys we already own without colliding with ERROR_HOTKEY_ALREADY_REGISTERED. */
    hotkey_unregister_all();

    rc = DialogBoxParamW(GetModuleHandleW(NULL),
        MAKEINTRESOURCEW(IDD_HOTKEY_SETTINGS),
        parent, hotkey_settings_dlg_proc, 0);

    if (rc == IDOK) {
        /* praxis_config has already been updated inside the dialog proc.
         * Persist it so the new bindings survive a restart. */
        save_profile_store();
    }

    /* Always re-register from praxis_config — on Cancel that restores the
     * pre-existing bindings; on OK that activates the new ones. */
    registered_ok = hk_reregister_all();
    if (!registered_ok && rc == IDOK) {
        MessageBoxW(parent,
            praxis_locale_str(STR_PRAXIS_HOTKEY_CONFLICT),
            praxis_locale_str(STR_PRAXIS_HOTKEY_SETTINGS),
            MB_OK | MB_ICONWARNING);
    }

    return rc == IDOK;
}
