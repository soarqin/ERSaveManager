/**
 * @file resource.h
 * @brief Praxis Win32 resource identifiers.
 */

#pragma once

/* Menu IDs */
#define IDR_MAIN_MENU       100
#define IDM_FILE_SET_ROOT   101
#define IDM_FILE_EXIT       103
#define IDM_BACKUP_FULL     110
#define IDM_BACKUP_SLOT     111
#define IDM_RESTORE_SEL     120
#define IDM_RESTORE_UNDO    121
#define IDM_RESTORE_SLOT    122
#define IDM_GAME_ER         130
#define IDM_GAME_MANAGE     131
#define IDM_GAME_PROFILE_FIRST 1300
#define IDM_GAME_PROFILE_LAST  1399
#define IDM_OPTIONS_HOTKEYS 140
#define IDM_OPTIONS_LANG    141
#define IDM_OPTIONS_COMP    142

/* Control IDs */
#define IDC_TREE_VIEW       200
#define IDC_STATUS_BAR      201

/* === Toolbar === */
#define IDC_TOOLBAR          210
#define IDC_PROFILE_COMBO    211
#define IDC_BTN_ADD_BACKUP   212
#define IDC_BTN_DEL_BACKUP   213
#define IDC_BTN_BACKUP_FULL  214
#define IDC_BTN_BACKUP_SLOT  215
#define IDC_BTN_RESTORE      216
#define IDC_BTN_UNDO         217

/* Dialog IDs */
#define IDD_HOTKEY_SETTINGS 300
#define IDD_GAME_PROFILE_MANAGER 310
#define IDD_EDIT_GAME_PROFILE    311
#define IDD_EDIT_BACKUP_PROFILE  312
#define IDD_MIGRATION_WIZARD     313

/* === Game Profile Manager Dialog Controls === */
#define IDC_GPM_LIST   4001
#define IDC_GPM_ADD    4002
#define IDC_GPM_EDIT   4003
#define IDC_GPM_DELETE 4004
#define IDC_GPM_CLOSE  4005

/* === Edit Game Profile Dialog Controls === */
#define IDC_EGP_NAME        4101
#define IDC_EGP_GAME        4102
#define IDC_EGP_SAVE_DIR    4103
#define IDC_EGP_TREE_ROOT   4104
#define IDC_EGP_BROWSE_SAVE 4105
#define IDC_EGP_BROWSE_TREE 4106

/* === Edit Backup Profile Dialog Controls === */
#define IDC_EBP_NAME       4201
#define IDC_EBP_TREE_ROOT  4202
#define IDC_EBP_BROWSE     4203
#define IDC_EBP_COMP_NONE  4204
#define IDC_EBP_COMP_LOW   4205
#define IDC_EBP_COMP_HIGH  4206

/* === Migration Wizard Dialog Controls === */
#define IDC_MW_LABEL      4301
#define IDC_MW_NAME       4302
#define IDC_MW_TREE_ROOT  4303
#define IDC_MW_BROWSE     4304
#define IDC_MW_COMP_GROUP 4305
#define IDC_MW_BACK       4306
#define IDC_MW_NEXT       4307
#define IDC_MW_FINISH     4308

/* Icon */
#define IDI_APPICON         1
