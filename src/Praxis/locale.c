/**
 * @file locale.c
 * @brief Praxis locale string table implementation.
 * @details Provides 11-language string table for the Praxis application.
 *          Non-English rows are English placeholders pending translation (v1).
 */

#include "locale.h"
#include "locale_core.h"

#include <windows.h>

/* Language names (11 languages matching ERSaveManager) */
static const wchar_t *praxis_language_names[11] = {
    L"English", L"Français", L"Deutsch", L"Italiano", L"Español",
    L"Português", L"Русский", L"日本語", L"한국어", L"简体中文", L"繁體中文"
};

/* BCP-47 codes for system language detection */
static const wchar_t *praxis_language_codes[11] = {
    L"en", L"fr", L"de", L"it", L"es",
    L"pt", L"ru", L"ja", L"ko", L"zh-Hans", L"zh-Hant"
};

/* Translation table [11 languages][STR_PRAXIS_MAX strings] */
static const wchar_t *praxis_locale_strings[11][STR_PRAXIS_MAX] = {
    /* 0: English */
    {
        L"Praxis - Practice Save Tool",          /* STR_PRAXIS_APP_TITLE */
        L"Backup Full Save",                     /* STR_PRAXIS_BACKUP_FULL */
        L"Restore Full Save",                    /* STR_PRAXIS_RESTORE_FULL */
        L"Backup Current Slot",                  /* STR_PRAXIS_BACKUP_SLOT */
        L"Restore Current Slot",                 /* STR_PRAXIS_RESTORE_SLOT */
        L"Undo Last Restore",                    /* STR_PRAXIS_UNDO_RESTORE */
        L"Tree Root",                            /* STR_PRAXIS_TREE_ROOT */
        L"New Folder",                           /* STR_PRAXIS_NEW_FOLDER */
        L"Rename",                               /* STR_PRAXIS_RENAME */
        L"Delete",                               /* STR_PRAXIS_DELETE */
        L"Hotkey Conflict",                      /* STR_PRAXIS_HOTKEY_CONFLICT */
        L"Close the game before restoring.",     /* STR_PRAXIS_GAME_RUNNING_WARNING */
        L"Ring backup failed. Restore aborted.", /* STR_PRAXIS_RING_BACKUP_FAILED */
        L"Restore this backup?",                 /* STR_PRAXIS_RESTORE_CONFIRMATION */
        L"Language",                             /* STR_PRAXIS_LANGUAGE */
        L"Options",                              /* STR_PRAXIS_OPTIONS */
        L"Hotkey Settings",                      /* STR_PRAXIS_HOTKEY_SETTINGS */
        L"Game",                                 /* STR_PRAXIS_GAME */
        L"File",                                 /* STR_PRAXIS_FILE */
        L"Confirm",                              /* STR_PRAXIS_CONFIRM */
        L"Cancel",                               /* STR_PRAXIS_CANCEL */
        L"Error",                                /* STR_PRAXIS_ERROR */
        L"Success",                              /* STR_PRAXIS_SUCCESS */
        L"Set Tree Root...",                     /* STR_PRAXIS_SET_TREE_ROOT */
        L"Refresh",                              /* STR_PRAXIS_REFRESH */
        L"Exit",                                 /* STR_PRAXIS_EXIT */
        L"Backup",                               /* STR_PRAXIS_BACKUP */
        L"Restore",                              /* STR_PRAXIS_RESTORE */
        L"Game Profile",                         /* STR_PRAXIS_GAME_PROFILE */
        L"Backup Profile",                       /* STR_PRAXIS_BACKUP_PROFILE */
        L"Manage Game Profiles...",              /* STR_PRAXIS_MANAGE_GAME_PROFILES */
        L"Name",                                 /* STR_PRAXIS_PROFILE_NAME */
        L"Game",                                 /* STR_PRAXIS_PROFILE_GAME */
        L"Save Directory",                       /* STR_PRAXIS_PROFILE_SAVE_DIR */
        L"Backup Root",                          /* STR_PRAXIS_PROFILE_TREE_ROOT */
        L"Compression",                          /* STR_PRAXIS_PROFILE_COMPRESSION */
        L"None",                                 /* STR_PRAXIS_COMPRESSION_NONE */
        L"Low",                                  /* STR_PRAXIS_COMPRESSION_LOW */
        L"Medium",                               /* STR_PRAXIS_COMPRESSION_MEDIUM */
        L"High",                                 /* STR_PRAXIS_COMPRESSION_HIGH */
        L"Add",                                  /* STR_PRAXIS_BTN_ADD */
        L"Edit",                                 /* STR_PRAXIS_BTN_EDIT */
        L"Delete",                               /* STR_PRAXIS_BTN_DELETE */
        L"Close",                                /* STR_PRAXIS_BTN_CLOSE */
        L"OK",                                   /* STR_PRAXIS_BTN_OK */
        L"Cancel",                               /* STR_PRAXIS_BTN_CANCEL */
        L"Backup Full Save",                     /* STR_PRAXIS_TIP_BACKUP_FULL */
        L"Backup Current Slot",                  /* STR_PRAXIS_TIP_BACKUP_SLOT */
        L"Restore",                              /* STR_PRAXIS_TIP_RESTORE */
        L"Undo Last Restore",                    /* STR_PRAXIS_TIP_UNDO */
        L"Add Backup Profile",                   /* STR_PRAXIS_TIP_ADD_BACKUP */
        L"Delete Backup Profile",                /* STR_PRAXIS_TIP_DELETE_BACKUP */
        L"Migration Wizard",                     /* STR_PRAXIS_MIGRATION_TITLE */
        L"Welcome to Praxis 2.0 - Multi-Profile Setup", /* STR_PRAXIS_MIGRATION_WELCOME */
        L"Step 1: Name your game profile",       /* STR_PRAXIS_MIGRATION_GAME_PAGE */
        L"Step 2: Configure your backup profile", /* STR_PRAXIS_MIGRATION_BACKUP_PAGE */
        L"Step 3: Confirm migration",            /* STR_PRAXIS_MIGRATION_CONFIRM */
        L"Delete this game profile and all its backup profiles?", /* STR_PRAXIS_CONFIRM_DELETE_GAME */
        L"Delete this backup profile?",          /* STR_PRAXIS_CONFIRM_DELETE_BACKUP */
        L"No profile selected. Use + to add a backup profile.", /* STR_PRAXIS_NO_PROFILE_HINT */
        L"Active: %s / %s",                      /* STR_PRAXIS_STATUS_ACTIVE */
        L"Show in File Explorer",                /* STR_PRAXIS_SHOW_IN_EXPLORER */
    },
    /* TODO: translate — 1: Français (placeholder: English) */
    {
        L"Praxis - Practice Save Tool",
        L"Backup Full Save",
        L"Restore Full Save",
        L"Backup Current Slot",
        L"Restore Current Slot",
        L"Undo Last Restore",
        L"Tree Root",
        L"New Folder",
        L"Rename",
        L"Delete",
        L"Hotkey Conflict",
        L"Close the game before restoring.",
        L"Ring backup failed. Restore aborted.",
        L"Restore this backup?",
        L"Language",
        L"Options",
        L"Hotkey Settings",
        L"Game",
        L"File",
        L"Confirm",
        L"Cancel",
        L"Error",
        L"Success",
        L"Set Tree Root...",
        L"Refresh",
        L"Exit",
        L"Backup",
        L"Restore",
        L"Game Profile", /* TODO: translate */
        L"Backup Profile", /* TODO: translate */
        L"Manage Game Profiles...", /* TODO: translate */
        L"Name", /* TODO: translate */
        L"Game", /* TODO: translate */
        L"Save Directory", /* TODO: translate */
        L"Backup Root", /* TODO: translate */
        L"Compression", /* TODO: translate */
        L"None", /* TODO: translate */
        L"Low", /* TODO: translate */
        L"Medium", /* TODO: translate */
        L"High", /* TODO: translate */
        L"Add", /* TODO: translate */
        L"Edit", /* TODO: translate */
        L"Delete", /* TODO: translate */
        L"Close", /* TODO: translate */
        L"OK", /* TODO: translate */
        L"Cancel", /* TODO: translate */
        L"Backup Full Save", /* TODO: translate */
        L"Backup Current Slot", /* TODO: translate */
        L"Restore", /* TODO: translate */
        L"Undo Last Restore", /* TODO: translate */
        L"Add Backup Profile", /* TODO: translate */
        L"Delete Backup Profile", /* TODO: translate */
        L"Migration Wizard", /* TODO: translate */
        L"Welcome to Praxis 2.0 - Multi-Profile Setup", /* TODO: translate */
        L"Step 1: Name your game profile", /* TODO: translate */
        L"Step 2: Configure your backup profile", /* TODO: translate */
        L"Step 3: Confirm migration", /* TODO: translate */
        L"Delete this game profile and all its backup profiles?", /* TODO: translate */
        L"Delete this backup profile?", /* TODO: translate */
        L"No profile selected. Use + to add a backup profile.", /* TODO: translate */
        L"Active: %s / %s", /* TODO: translate */
        L"Show in File Explorer", /* TODO: translate — STR_PRAXIS_SHOW_IN_EXPLORER */
    },
    /* TODO: translate — 2: Deutsch (placeholder: English) */
    {
        L"Praxis - Practice Save Tool",
        L"Backup Full Save",
        L"Restore Full Save",
        L"Backup Current Slot",
        L"Restore Current Slot",
        L"Undo Last Restore",
        L"Tree Root",
        L"New Folder",
        L"Rename",
        L"Delete",
        L"Hotkey Conflict",
        L"Close the game before restoring.",
        L"Ring backup failed. Restore aborted.",
        L"Restore this backup?",
        L"Language",
        L"Options",
        L"Hotkey Settings",
        L"Game",
        L"File",
        L"Confirm",
        L"Cancel",
        L"Error",
        L"Success",
        L"Set Tree Root...",
        L"Refresh",
        L"Exit",
        L"Backup",
        L"Restore",
        L"Game Profile", /* TODO: translate */
        L"Backup Profile", /* TODO: translate */
        L"Manage Game Profiles...", /* TODO: translate */
        L"Name", /* TODO: translate */
        L"Game", /* TODO: translate */
        L"Save Directory", /* TODO: translate */
        L"Backup Root", /* TODO: translate */
        L"Compression", /* TODO: translate */
        L"None", /* TODO: translate */
        L"Low", /* TODO: translate */
        L"Medium", /* TODO: translate */
        L"High", /* TODO: translate */
        L"Add", /* TODO: translate */
        L"Edit", /* TODO: translate */
        L"Delete", /* TODO: translate */
        L"Close", /* TODO: translate */
        L"OK", /* TODO: translate */
        L"Cancel", /* TODO: translate */
        L"Backup Full Save", /* TODO: translate */
        L"Backup Current Slot", /* TODO: translate */
        L"Restore", /* TODO: translate */
        L"Undo Last Restore", /* TODO: translate */
        L"Add Backup Profile", /* TODO: translate */
        L"Delete Backup Profile", /* TODO: translate */
        L"Migration Wizard", /* TODO: translate */
        L"Welcome to Praxis 2.0 - Multi-Profile Setup", /* TODO: translate */
        L"Step 1: Name your game profile", /* TODO: translate */
        L"Step 2: Configure your backup profile", /* TODO: translate */
        L"Step 3: Confirm migration", /* TODO: translate */
        L"Delete this game profile and all its backup profiles?", /* TODO: translate */
        L"Delete this backup profile?", /* TODO: translate */
        L"No profile selected. Use + to add a backup profile.", /* TODO: translate */
        L"Active: %s / %s", /* TODO: translate */
        L"Show in File Explorer", /* TODO: translate — STR_PRAXIS_SHOW_IN_EXPLORER */
    },
    /* TODO: translate — 3: Italiano (placeholder: English) */
    {
        L"Praxis - Practice Save Tool",
        L"Backup Full Save",
        L"Restore Full Save",
        L"Backup Current Slot",
        L"Restore Current Slot",
        L"Undo Last Restore",
        L"Tree Root",
        L"New Folder",
        L"Rename",
        L"Delete",
        L"Hotkey Conflict",
        L"Close the game before restoring.",
        L"Ring backup failed. Restore aborted.",
        L"Restore this backup?",
        L"Language",
        L"Options",
        L"Hotkey Settings",
        L"Game",
        L"File",
        L"Confirm",
        L"Cancel",
        L"Error",
        L"Success",
        L"Set Tree Root...",
        L"Refresh",
        L"Exit",
        L"Backup",
        L"Restore",
        L"Game Profile", /* TODO: translate */
        L"Backup Profile", /* TODO: translate */
        L"Manage Game Profiles...", /* TODO: translate */
        L"Name", /* TODO: translate */
        L"Game", /* TODO: translate */
        L"Save Directory", /* TODO: translate */
        L"Backup Root", /* TODO: translate */
        L"Compression", /* TODO: translate */
        L"None", /* TODO: translate */
        L"Low", /* TODO: translate */
        L"Medium", /* TODO: translate */
        L"High", /* TODO: translate */
        L"Add", /* TODO: translate */
        L"Edit", /* TODO: translate */
        L"Delete", /* TODO: translate */
        L"Close", /* TODO: translate */
        L"OK", /* TODO: translate */
        L"Cancel", /* TODO: translate */
        L"Backup Full Save", /* TODO: translate */
        L"Backup Current Slot", /* TODO: translate */
        L"Restore", /* TODO: translate */
        L"Undo Last Restore", /* TODO: translate */
        L"Add Backup Profile", /* TODO: translate */
        L"Delete Backup Profile", /* TODO: translate */
        L"Migration Wizard", /* TODO: translate */
        L"Welcome to Praxis 2.0 - Multi-Profile Setup", /* TODO: translate */
        L"Step 1: Name your game profile", /* TODO: translate */
        L"Step 2: Configure your backup profile", /* TODO: translate */
        L"Step 3: Confirm migration", /* TODO: translate */
        L"Delete this game profile and all its backup profiles?", /* TODO: translate */
        L"Delete this backup profile?", /* TODO: translate */
        L"No profile selected. Use + to add a backup profile.", /* TODO: translate */
        L"Active: %s / %s", /* TODO: translate */
        L"Show in File Explorer", /* TODO: translate — STR_PRAXIS_SHOW_IN_EXPLORER */
    },
    /* TODO: translate — 4: Español (placeholder: English) */
    {
        L"Praxis - Practice Save Tool",
        L"Backup Full Save",
        L"Restore Full Save",
        L"Backup Current Slot",
        L"Restore Current Slot",
        L"Undo Last Restore",
        L"Tree Root",
        L"New Folder",
        L"Rename",
        L"Delete",
        L"Hotkey Conflict",
        L"Close the game before restoring.",
        L"Ring backup failed. Restore aborted.",
        L"Restore this backup?",
        L"Language",
        L"Options",
        L"Hotkey Settings",
        L"Game",
        L"File",
        L"Confirm",
        L"Cancel",
        L"Error",
        L"Success",
        L"Set Tree Root...",
        L"Refresh",
        L"Exit",
        L"Backup",
        L"Restore",
        L"Game Profile", /* TODO: translate */
        L"Backup Profile", /* TODO: translate */
        L"Manage Game Profiles...", /* TODO: translate */
        L"Name", /* TODO: translate */
        L"Game", /* TODO: translate */
        L"Save Directory", /* TODO: translate */
        L"Backup Root", /* TODO: translate */
        L"Compression", /* TODO: translate */
        L"None", /* TODO: translate */
        L"Low", /* TODO: translate */
        L"Medium", /* TODO: translate */
        L"High", /* TODO: translate */
        L"Add", /* TODO: translate */
        L"Edit", /* TODO: translate */
        L"Delete", /* TODO: translate */
        L"Close", /* TODO: translate */
        L"OK", /* TODO: translate */
        L"Cancel", /* TODO: translate */
        L"Backup Full Save", /* TODO: translate */
        L"Backup Current Slot", /* TODO: translate */
        L"Restore", /* TODO: translate */
        L"Undo Last Restore", /* TODO: translate */
        L"Add Backup Profile", /* TODO: translate */
        L"Delete Backup Profile", /* TODO: translate */
        L"Migration Wizard", /* TODO: translate */
        L"Welcome to Praxis 2.0 - Multi-Profile Setup", /* TODO: translate */
        L"Step 1: Name your game profile", /* TODO: translate */
        L"Step 2: Configure your backup profile", /* TODO: translate */
        L"Step 3: Confirm migration", /* TODO: translate */
        L"Delete this game profile and all its backup profiles?", /* TODO: translate */
        L"Delete this backup profile?", /* TODO: translate */
        L"No profile selected. Use + to add a backup profile.", /* TODO: translate */
        L"Active: %s / %s", /* TODO: translate */
        L"Show in File Explorer", /* TODO: translate — STR_PRAXIS_SHOW_IN_EXPLORER */
    },
    /* TODO: translate — 5: Português (placeholder: English) */
    {
        L"Praxis - Practice Save Tool",
        L"Backup Full Save",
        L"Restore Full Save",
        L"Backup Current Slot",
        L"Restore Current Slot",
        L"Undo Last Restore",
        L"Tree Root",
        L"New Folder",
        L"Rename",
        L"Delete",
        L"Hotkey Conflict",
        L"Close the game before restoring.",
        L"Ring backup failed. Restore aborted.",
        L"Restore this backup?",
        L"Language",
        L"Options",
        L"Hotkey Settings",
        L"Game",
        L"File",
        L"Confirm",
        L"Cancel",
        L"Error",
        L"Success",
        L"Set Tree Root...",
        L"Refresh",
        L"Exit",
        L"Backup",
        L"Restore",
        L"Game Profile", /* TODO: translate */
        L"Backup Profile", /* TODO: translate */
        L"Manage Game Profiles...", /* TODO: translate */
        L"Name", /* TODO: translate */
        L"Game", /* TODO: translate */
        L"Save Directory", /* TODO: translate */
        L"Backup Root", /* TODO: translate */
        L"Compression", /* TODO: translate */
        L"None", /* TODO: translate */
        L"Low", /* TODO: translate */
        L"Medium", /* TODO: translate */
        L"High", /* TODO: translate */
        L"Add", /* TODO: translate */
        L"Edit", /* TODO: translate */
        L"Delete", /* TODO: translate */
        L"Close", /* TODO: translate */
        L"OK", /* TODO: translate */
        L"Cancel", /* TODO: translate */
        L"Backup Full Save", /* TODO: translate */
        L"Backup Current Slot", /* TODO: translate */
        L"Restore", /* TODO: translate */
        L"Undo Last Restore", /* TODO: translate */
        L"Add Backup Profile", /* TODO: translate */
        L"Delete Backup Profile", /* TODO: translate */
        L"Migration Wizard", /* TODO: translate */
        L"Welcome to Praxis 2.0 - Multi-Profile Setup", /* TODO: translate */
        L"Step 1: Name your game profile", /* TODO: translate */
        L"Step 2: Configure your backup profile", /* TODO: translate */
        L"Step 3: Confirm migration", /* TODO: translate */
        L"Delete this game profile and all its backup profiles?", /* TODO: translate */
        L"Delete this backup profile?", /* TODO: translate */
        L"No profile selected. Use + to add a backup profile.", /* TODO: translate */
        L"Active: %s / %s", /* TODO: translate */
        L"Show in File Explorer", /* TODO: translate — STR_PRAXIS_SHOW_IN_EXPLORER */
    },
    /* TODO: translate — 6: Русский (placeholder: English) */
    {
        L"Praxis - Practice Save Tool",
        L"Backup Full Save",
        L"Restore Full Save",
        L"Backup Current Slot",
        L"Restore Current Slot",
        L"Undo Last Restore",
        L"Tree Root",
        L"New Folder",
        L"Rename",
        L"Delete",
        L"Hotkey Conflict",
        L"Close the game before restoring.",
        L"Ring backup failed. Restore aborted.",
        L"Restore this backup?",
        L"Language",
        L"Options",
        L"Hotkey Settings",
        L"Game",
        L"File",
        L"Confirm",
        L"Cancel",
        L"Error",
        L"Success",
        L"Set Tree Root...",
        L"Refresh",
        L"Exit",
        L"Backup",
        L"Restore",
        L"Game Profile", /* TODO: translate */
        L"Backup Profile", /* TODO: translate */
        L"Manage Game Profiles...", /* TODO: translate */
        L"Name", /* TODO: translate */
        L"Game", /* TODO: translate */
        L"Save Directory", /* TODO: translate */
        L"Backup Root", /* TODO: translate */
        L"Compression", /* TODO: translate */
        L"None", /* TODO: translate */
        L"Low", /* TODO: translate */
        L"Medium", /* TODO: translate */
        L"High", /* TODO: translate */
        L"Add", /* TODO: translate */
        L"Edit", /* TODO: translate */
        L"Delete", /* TODO: translate */
        L"Close", /* TODO: translate */
        L"OK", /* TODO: translate */
        L"Cancel", /* TODO: translate */
        L"Backup Full Save", /* TODO: translate */
        L"Backup Current Slot", /* TODO: translate */
        L"Restore", /* TODO: translate */
        L"Undo Last Restore", /* TODO: translate */
        L"Add Backup Profile", /* TODO: translate */
        L"Delete Backup Profile", /* TODO: translate */
        L"Migration Wizard", /* TODO: translate */
        L"Welcome to Praxis 2.0 - Multi-Profile Setup", /* TODO: translate */
        L"Step 1: Name your game profile", /* TODO: translate */
        L"Step 2: Configure your backup profile", /* TODO: translate */
        L"Step 3: Confirm migration", /* TODO: translate */
        L"Delete this game profile and all its backup profiles?", /* TODO: translate */
        L"Delete this backup profile?", /* TODO: translate */
        L"No profile selected. Use + to add a backup profile.", /* TODO: translate */
        L"Active: %s / %s", /* TODO: translate */
        L"Show in File Explorer", /* TODO: translate — STR_PRAXIS_SHOW_IN_EXPLORER */
    },
    /* TODO: translate — 7: 日本語 (placeholder: English) */
    {
        L"Praxis - Practice Save Tool",
        L"Backup Full Save",
        L"Restore Full Save",
        L"Backup Current Slot",
        L"Restore Current Slot",
        L"Undo Last Restore",
        L"Tree Root",
        L"New Folder",
        L"Rename",
        L"Delete",
        L"Hotkey Conflict",
        L"Close the game before restoring.",
        L"Ring backup failed. Restore aborted.",
        L"Restore this backup?",
        L"Language",
        L"Options",
        L"Hotkey Settings",
        L"Game",
        L"File",
        L"Confirm",
        L"Cancel",
        L"Error",
        L"Success",
        L"Set Tree Root...",
        L"Refresh",
        L"Exit",
        L"Backup",
        L"Restore",
        L"Game Profile", /* TODO: translate */
        L"Backup Profile", /* TODO: translate */
        L"Manage Game Profiles...", /* TODO: translate */
        L"Name", /* TODO: translate */
        L"Game", /* TODO: translate */
        L"Save Directory", /* TODO: translate */
        L"Backup Root", /* TODO: translate */
        L"Compression", /* TODO: translate */
        L"None", /* TODO: translate */
        L"Low", /* TODO: translate */
        L"Medium", /* TODO: translate */
        L"High", /* TODO: translate */
        L"Add", /* TODO: translate */
        L"Edit", /* TODO: translate */
        L"Delete", /* TODO: translate */
        L"Close", /* TODO: translate */
        L"OK", /* TODO: translate */
        L"Cancel", /* TODO: translate */
        L"Backup Full Save", /* TODO: translate */
        L"Backup Current Slot", /* TODO: translate */
        L"Restore", /* TODO: translate */
        L"Undo Last Restore", /* TODO: translate */
        L"Add Backup Profile", /* TODO: translate */
        L"Delete Backup Profile", /* TODO: translate */
        L"Migration Wizard", /* TODO: translate */
        L"Welcome to Praxis 2.0 - Multi-Profile Setup", /* TODO: translate */
        L"Step 1: Name your game profile", /* TODO: translate */
        L"Step 2: Configure your backup profile", /* TODO: translate */
        L"Step 3: Confirm migration", /* TODO: translate */
        L"Delete this game profile and all its backup profiles?", /* TODO: translate */
        L"Delete this backup profile?", /* TODO: translate */
        L"No profile selected. Use + to add a backup profile.", /* TODO: translate */
        L"Active: %s / %s", /* TODO: translate */
        L"Show in File Explorer", /* TODO: translate — STR_PRAXIS_SHOW_IN_EXPLORER */
    },
    /* TODO: translate — 8: 한국어 (placeholder: English) */
    {
        L"Praxis - Practice Save Tool",
        L"Backup Full Save",
        L"Restore Full Save",
        L"Backup Current Slot",
        L"Restore Current Slot",
        L"Undo Last Restore",
        L"Tree Root",
        L"New Folder",
        L"Rename",
        L"Delete",
        L"Hotkey Conflict",
        L"Close the game before restoring.",
        L"Ring backup failed. Restore aborted.",
        L"Restore this backup?",
        L"Language",
        L"Options",
        L"Hotkey Settings",
        L"Game",
        L"File",
        L"Confirm",
        L"Cancel",
        L"Error",
        L"Success",
        L"Set Tree Root...",
        L"Refresh",
        L"Exit",
        L"Backup",
        L"Restore",
        L"Game Profile", /* TODO: translate */
        L"Backup Profile", /* TODO: translate */
        L"Manage Game Profiles...", /* TODO: translate */
        L"Name", /* TODO: translate */
        L"Game", /* TODO: translate */
        L"Save Directory", /* TODO: translate */
        L"Backup Root", /* TODO: translate */
        L"Compression", /* TODO: translate */
        L"None", /* TODO: translate */
        L"Low", /* TODO: translate */
        L"Medium", /* TODO: translate */
        L"High", /* TODO: translate */
        L"Add", /* TODO: translate */
        L"Edit", /* TODO: translate */
        L"Delete", /* TODO: translate */
        L"Close", /* TODO: translate */
        L"OK", /* TODO: translate */
        L"Cancel", /* TODO: translate */
        L"Backup Full Save", /* TODO: translate */
        L"Backup Current Slot", /* TODO: translate */
        L"Restore", /* TODO: translate */
        L"Undo Last Restore", /* TODO: translate */
        L"Add Backup Profile", /* TODO: translate */
        L"Delete Backup Profile", /* TODO: translate */
        L"Migration Wizard", /* TODO: translate */
        L"Welcome to Praxis 2.0 - Multi-Profile Setup", /* TODO: translate */
        L"Step 1: Name your game profile", /* TODO: translate */
        L"Step 2: Configure your backup profile", /* TODO: translate */
        L"Step 3: Confirm migration", /* TODO: translate */
        L"Delete this game profile and all its backup profiles?", /* TODO: translate */
        L"Delete this backup profile?", /* TODO: translate */
        L"No profile selected. Use + to add a backup profile.", /* TODO: translate */
        L"Active: %s / %s", /* TODO: translate */
        L"Show in File Explorer", /* TODO: translate — STR_PRAXIS_SHOW_IN_EXPLORER */
    },
    /* TODO: translate — 9: 简体中文 (placeholder: English) */
    {
        L"Praxis - Practice Save Tool",
        L"Backup Full Save",
        L"Restore Full Save",
        L"Backup Current Slot",
        L"Restore Current Slot",
        L"Undo Last Restore",
        L"Tree Root",
        L"New Folder",
        L"Rename",
        L"Delete",
        L"Hotkey Conflict",
        L"Close the game before restoring.",
        L"Ring backup failed. Restore aborted.",
        L"Restore this backup?",
        L"Language",
        L"Options",
        L"Hotkey Settings",
        L"Game",
        L"File",
        L"Confirm",
        L"Cancel",
        L"Error",
        L"Success",
        L"Set Tree Root...",
        L"Refresh",
        L"Exit",
        L"Backup",
        L"Restore",
        L"Game Profile", /* TODO: translate */
        L"Backup Profile", /* TODO: translate */
        L"Manage Game Profiles...", /* TODO: translate */
        L"Name", /* TODO: translate */
        L"Game", /* TODO: translate */
        L"Save Directory", /* TODO: translate */
        L"Backup Root", /* TODO: translate */
        L"Compression", /* TODO: translate */
        L"None", /* TODO: translate */
        L"Low", /* TODO: translate */
        L"Medium", /* TODO: translate */
        L"High", /* TODO: translate */
        L"Add", /* TODO: translate */
        L"Edit", /* TODO: translate */
        L"Delete", /* TODO: translate */
        L"Close", /* TODO: translate */
        L"OK", /* TODO: translate */
        L"Cancel", /* TODO: translate */
        L"Backup Full Save", /* TODO: translate */
        L"Backup Current Slot", /* TODO: translate */
        L"Restore", /* TODO: translate */
        L"Undo Last Restore", /* TODO: translate */
        L"Add Backup Profile", /* TODO: translate */
        L"Delete Backup Profile", /* TODO: translate */
        L"Migration Wizard", /* TODO: translate */
        L"Welcome to Praxis 2.0 - Multi-Profile Setup", /* TODO: translate */
        L"Step 1: Name your game profile", /* TODO: translate */
        L"Step 2: Configure your backup profile", /* TODO: translate */
        L"Step 3: Confirm migration", /* TODO: translate */
        L"Delete this game profile and all its backup profiles?", /* TODO: translate */
        L"Delete this backup profile?", /* TODO: translate */
        L"No profile selected. Use + to add a backup profile.", /* TODO: translate */
        L"Active: %s / %s", /* TODO: translate */
        L"在文件浏览器中显示",     /* STR_PRAXIS_SHOW_IN_EXPLORER */
    },
    /* TODO: translate — 10: 繁體中文 (placeholder: English) */
    {
        L"Praxis - Practice Save Tool",
        L"Backup Full Save",
        L"Restore Full Save",
        L"Backup Current Slot",
        L"Restore Current Slot",
        L"Undo Last Restore",
        L"Tree Root",
        L"New Folder",
        L"Rename",
        L"Delete",
        L"Hotkey Conflict",
        L"Close the game before restoring.",
        L"Ring backup failed. Restore aborted.",
        L"Restore this backup?",
        L"Language",
        L"Options",
        L"Hotkey Settings",
        L"Game",
        L"File",
        L"Confirm",
        L"Cancel",
        L"Error",
        L"Success",
        L"Set Tree Root...",
        L"Refresh",
        L"Exit",
        L"Backup",
        L"Restore",
        L"Game Profile", /* TODO: translate */
        L"Backup Profile", /* TODO: translate */
        L"Manage Game Profiles...", /* TODO: translate */
        L"Name", /* TODO: translate */
        L"Game", /* TODO: translate */
        L"Save Directory", /* TODO: translate */
        L"Backup Root", /* TODO: translate */
        L"Compression", /* TODO: translate */
        L"None", /* TODO: translate */
        L"Low", /* TODO: translate */
        L"Medium", /* TODO: translate */
        L"High", /* TODO: translate */
        L"Add", /* TODO: translate */
        L"Edit", /* TODO: translate */
        L"Delete", /* TODO: translate */
        L"Close", /* TODO: translate */
        L"OK", /* TODO: translate */
        L"Cancel", /* TODO: translate */
        L"Backup Full Save", /* TODO: translate */
        L"Backup Current Slot", /* TODO: translate */
        L"Restore", /* TODO: translate */
        L"Undo Last Restore", /* TODO: translate */
        L"Add Backup Profile", /* TODO: translate */
        L"Delete Backup Profile", /* TODO: translate */
        L"Migration Wizard", /* TODO: translate */
        L"Welcome to Praxis 2.0 - Multi-Profile Setup", /* TODO: translate */
        L"Step 1: Name your game profile", /* TODO: translate */
        L"Step 2: Configure your backup profile", /* TODO: translate */
        L"Step 3: Confirm migration", /* TODO: translate */
        L"Delete this game profile and all its backup profiles?", /* TODO: translate */
        L"Delete this backup profile?", /* TODO: translate */
        L"No profile selected. Use + to add a backup profile.", /* TODO: translate */
        L"Active: %s / %s", /* TODO: translate */
        L"在檔案總管中顯示",       /* STR_PRAXIS_SHOW_IN_EXPLORER */
    },
};

/**
 * @brief Gets a localized string by index.
 * @param idx Index of the string to retrieve.
 * @return Pointer to the localized wide string.
 */
const wchar_t *praxis_locale_str(praxis_string_index_t idx) {
    int cur = locale_core_get_current();
    if (cur < 0 || cur >= 11) cur = 0;
    if (idx < 0 || idx >= STR_PRAXIS_MAX) return L"";
    return praxis_locale_strings[cur][idx];
}

/**
 * @brief Gets the total number of available locales.
 * @return Number of available locales.
 */
int praxis_locale_count(void) { return 11; }

/**
 * @brief Gets the name of a locale by index.
 * @param idx Index of the locale.
 * @return Pointer to the locale name string.
 */
const wchar_t *praxis_locale_name(int idx) {
    if (idx < 0 || idx >= 11) return L"";
    return praxis_language_names[idx];
}

/**
 * @brief Gets the current locale index.
 * @return Current locale index.
 */
int praxis_locale_get_current(void) { return locale_core_get_current(); }

/**
 * @brief Sets the current locale index.
 * @param idx The locale index to set as current.
 */
void praxis_locale_set_current(int idx) { locale_core_set_current(idx); }

/**
 * @brief Detects the system language and returns the best matching language index.
 * @return Best matching language index.
 */
int praxis_locale_detect_system(void) {
    return locale_core_detect_system_language(0, praxis_language_codes, 11);
}
