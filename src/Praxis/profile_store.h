/**
 * @file profile_store.h
 * @brief Multi-profile system for Praxis: game profiles, backup profiles, and profile store.
 * @details Defines data structures for managing multiple game configurations and their
 *          associated backup profiles. All data is stack-allocated with fixed-size arrays.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <windows.h>

#include "game_backend.h"

/* Compression level enumeration */
typedef enum compression_level_e {
    COMP_LEVEL_NONE = 0,
    COMP_LEVEL_LOW  = 1,
    COMP_LEVEL_HIGH = 2
} compression_level_t;

/* Maximum profile counts */
#define MAX_GAME_PROFILES 32
#define MAX_BACKUP_PROFILES 128

/**
 * @brief Game profile: represents a configured game instance.
 * @details Stores game-specific settings including save directory and backup root.
 *          id >= 1 is valid; id == 0 is invalid/uninitialized.
 */
typedef struct game_profile_s {
    int id;                                 /* Sequential ID; >= 1 is valid, 0 = invalid */
    wchar_t name[64];                       /* User-visible name; duplicates allowed */
    game_id_t game_id;                      /* Game type (e.g., GAME_ID_ELDEN_RING) */
    wchar_t original_save_dir[MAX_PATH];    /* Game save directory; empty = auto-discover */
    wchar_t tree_root[MAX_PATH];            /* Base directory for backup profiles */
} game_profile_t;

/**
 * @brief Backup profile: represents a named backup configuration for a game.
 * @details Stores backup-specific settings including compression level and tree root.
 *          id >= 1 is valid; id == 0 is invalid/uninitialized.
 */
typedef struct backup_profile_s {
    int id;                                 /* Sequential ID; >= 1 is valid */
    int parent_game_id;                     /* References game_profile_t.id */
    wchar_t name[64];                       /* User-visible name */
    wchar_t tree_root[MAX_PATH];            /* Backup directory; defaults to <game.tree_root>/<name> */
    compression_level_t compression_level;  /* Compression level for backups */
} backup_profile_t;

/**
 * @brief Profile store: container for all game and backup profiles.
 * @details Manages the complete set of game profiles, backup profiles, and active selections.
 *          All data is stack-allocated; no heap pointers.
 */
typedef struct profile_store_s {
    game_profile_t games[MAX_GAME_PROFILES];        /* Array of game profiles */
    size_t game_count;                              /* Number of valid game profiles */
    backup_profile_t backups[MAX_BACKUP_PROFILES];  /* Array of backup profiles */
    size_t backup_count;                            /* Number of valid backup profiles */
    int active_game_id;                             /* Currently active game profile ID */
    int active_backup_id;                           /* Currently active backup profile ID */
    int next_game_id;                               /* Next ID to assign to new game profile */
    int next_backup_id;                             /* Next ID to assign to new backup profile */
} profile_store_t;

/**
 * @brief Initialize a profile store to empty state.
 * @param store Pointer to profile_store_t to initialize.
 */
void profile_store_init(profile_store_t *store);

/**
 * @brief Load profile store from INI file.
 * @param store Pointer to profile_store_t to populate.
 * @param ini_path Path to INI file.
 * @return true on success, false on failure.
 */
bool profile_store_load(profile_store_t *store, const wchar_t *ini_path);

/**
 * @brief Save profile store to INI file.
 * @param store Pointer to profile_store_t to save.
 * @param ini_path Path to INI file.
 * @return true on success, false on failure.
 */
bool profile_store_save(const profile_store_t *store, const wchar_t *ini_path);

/**
 * @brief Add a new game profile to the store.
 * @param store Pointer to profile_store_t.
 * @param gp Pointer to game_profile_t to add (id field is ignored; auto-assigned).
 * @return Assigned game profile ID on success, 0 on failure (store full).
 */
int profile_store_add_game(profile_store_t *store, const game_profile_t *gp);

/**
 * @brief Update an existing game profile.
 * @param store Pointer to profile_store_t.
 * @param id Game profile ID to update.
 * @param gp Pointer to game_profile_t with new values.
 * @return true on success, false if ID not found.
 */
bool profile_store_update_game(profile_store_t *store, int id, const game_profile_t *gp);

/**
 * @brief Delete a game profile and all associated backup profiles.
 * @param store Pointer to profile_store_t.
 * @param id Game profile ID to delete.
 * @return true on success, false if ID not found.
 */
bool profile_store_delete_game(profile_store_t *store, int id);

/**
 * @brief Add a new backup profile to the store.
 * @param store Pointer to profile_store_t.
 * @param bp Pointer to backup_profile_t to add (id field is ignored; auto-assigned).
 * @return Assigned backup profile ID on success, 0 on failure (store full).
 */
int profile_store_add_backup(profile_store_t *store, const backup_profile_t *bp);

/**
 * @brief Update an existing backup profile.
 * @param store Pointer to profile_store_t.
 * @param id Backup profile ID to update.
 * @param bp Pointer to backup_profile_t with new values.
 * @return true on success, false if ID not found.
 */
bool profile_store_update_backup(profile_store_t *store, int id, const backup_profile_t *bp);

/**
 * @brief Delete a backup profile.
 * @param store Pointer to profile_store_t.
 * @param id Backup profile ID to delete.
 * @return true on success, false if ID not found.
 */
bool profile_store_delete_backup(profile_store_t *store, int id);

/**
 * @brief Set the active game profile.
 * @param store Pointer to profile_store_t.
 * @param id Game profile ID to activate.
 * @return true on success, false if ID not found.
 */
bool profile_store_set_active_game(profile_store_t *store, int id);

/**
 * @brief Set the active backup profile.
 * @param store Pointer to profile_store_t.
 * @param id Backup profile ID to activate.
 * @return true on success, false if ID not found.
 */
bool profile_store_set_active_backup(profile_store_t *store, int id);

/**
 * @brief Get the currently active game profile.
 * @param store Pointer to profile_store_t.
 * @return Pointer to active game_profile_t, or NULL if none is active.
 */
const game_profile_t *profile_store_get_active_game(const profile_store_t *store);

/**
 * @brief Get the currently active backup profile.
 * @param store Pointer to profile_store_t.
 * @return Pointer to active backup_profile_t, or NULL if none is active.
 */
const backup_profile_t *profile_store_get_active_backup(const profile_store_t *store);

/**
 * @brief List all backup profiles for a given game.
 * @param store Pointer to profile_store_t.
 * @param game_id Game profile ID to filter by.
 * @param out Array to populate with pointers to backup_profile_t.
 * @param out_cap Capacity of out array.
 * @return Number of backup profiles found (may exceed out_cap if truncated).
 */
size_t profile_store_list_backups_for_game(const profile_store_t *store, int game_id, const backup_profile_t **out, size_t out_cap);
