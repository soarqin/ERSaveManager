/**
 * @file profile_store.c
 * @brief Multi-profile system I/O for Praxis: load and save profile store from INI.
 * @details Implements all profile_store_t CRUD helpers plus profile_store_load and
 *          profile_store_save. Loading uses config_core_parse_ini_ex for multi-section
 *          parsing. Saving writes a complete INI file atomically via a temp file and
 *          MoveFileExW. Orphan backup profiles (whose parent game no longer exists)
 *          are silently removed during load.
 */

#include "profile_store.h"
#include "config.h"
#include "config_core.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <windows.h>
#include <shlobj.h>

/* Maximum INI file size accepted by profile_store_load (256 KiB). */
#define PROFILE_STORE_MAX_BYTES (256u * 1024u)

/* ==== Init ==== */

void profile_store_init(profile_store_t *store) {
    if (store == NULL) {
        return;
    }
    ZeroMemory(store, sizeof(*store));
    store->next_game_id = 1;
    store->next_backup_id = 1;
}

/* ==== Accessors ==== */

const game_profile_t *profile_store_get_active_game(const profile_store_t *store) {
    if (store == NULL || store->active_game_id == 0) {
        return NULL;
    }
    for (size_t i = 0; i < store->game_count; i++) {
        if (store->games[i].id == store->active_game_id) {
            return &store->games[i];
        }
    }
    return NULL;
}

const backup_profile_t *profile_store_get_active_backup(const profile_store_t *store) {
    if (store == NULL || store->active_backup_id == 0) {
        return NULL;
    }
    for (size_t i = 0; i < store->backup_count; i++) {
        if (store->backups[i].id == store->active_backup_id) {
            return &store->backups[i];
        }
    }
    return NULL;
}

/* ==== Game profile CRUD ==== */

int profile_store_add_game(profile_store_t *store, const game_profile_t *gp) {
    if (store == NULL || gp == NULL) {
        return 0;
    }
    if (store->game_count >= MAX_GAME_PROFILES) {
        return 0;
    }
    /* Validate required fields. */
    if (gp->name[0] == L'\0' || gp->tree_root[0] == L'\0') {
        return 0;
    }

    /* Assign a new sequential ID. */
    int id = store->next_game_id++;
    store->games[store->game_count] = *gp;
    store->games[store->game_count].id = id;
    store->game_count++;

    /* Create tree_root directory if it does not already exist. */
    SHCreateDirectoryExW(NULL, gp->tree_root, NULL);
    /* Ignore return value — ERROR_ALREADY_EXISTS is acceptable. */

    /* Auto-create a Main backup profile for this game. */
    backup_profile_t main_bp;
    ZeroMemory(&main_bp, sizeof(main_bp));
    main_bp.parent_game_id = id;
    lstrcpyW(main_bp.name, L"Main");
    /* tree_root for Main = <game.tree_root>\Main */
    _snwprintf(main_bp.tree_root, MAX_PATH, L"%ls\\Main", gp->tree_root);
    main_bp.tree_root[MAX_PATH - 1] = L'\0';
    main_bp.compression_level = COMP_LEVEL_LOW;

    /* Create the Main backup directory. */
    SHCreateDirectoryExW(NULL, main_bp.tree_root, NULL);

    /* Inline-add the backup, bypassing parent validation (parent was just added). */
    int backup_id = store->next_backup_id++;
    if (store->backup_count < MAX_BACKUP_PROFILES) {
        main_bp.id = backup_id;
        store->backups[store->backup_count++] = main_bp;
    }

    /* Set both new profiles as active. */
    store->active_game_id = id;
    store->active_backup_id = backup_id;

    return id;
}

bool profile_store_update_game(profile_store_t *store, int id, const game_profile_t *gp) {
    if (store == NULL || gp == NULL || id <= 0) {
        return false;
    }
    for (size_t i = 0; i < store->game_count; i++) {
        if (store->games[i].id == id) {
            store->games[i] = *gp;
            store->games[i].id = id; /* preserve the original ID */
            return true;
        }
    }
    return false;
}

bool profile_store_delete_game(profile_store_t *store, int id) {
    if (store == NULL || id <= 0) {
        return false;
    }

    /* Find the game entry. */
    size_t idx = store->game_count;
    for (size_t i = 0; i < store->game_count; i++) {
        if (store->games[i].id == id) {
            idx = i;
            break;
        }
    }
    if (idx == store->game_count) {
        return false;
    }

    /* Compact the game array. */
    for (size_t i = idx; i + 1 < store->game_count; i++) {
        store->games[i] = store->games[i + 1];
    }
    store->game_count--;

    /* Remove all backup profiles whose parent is the deleted game. */
    size_t j = 0;
    while (j < store->backup_count) {
        if (store->backups[j].parent_game_id == id) {
            int removed_id = store->backups[j].id;
            for (size_t k = j; k + 1 < store->backup_count; k++) {
                store->backups[k] = store->backups[k + 1];
            }
            store->backup_count--;
            /* Clear active backup if it was just removed. */
            if (store->active_backup_id == removed_id) {
                store->active_backup_id = store->backup_count > 0 ? store->backups[0].id : 0;
            }
        } else {
            j++;
        }
    }

    /* Clear active game ID if it was the deleted game. */
    if (store->active_game_id == id) {
        store->active_game_id = store->game_count > 0 ? store->games[0].id : 0;
    }

    return true;
}

/* ==== Backup profile CRUD ==== */

int profile_store_add_backup(profile_store_t *store, const backup_profile_t *bp) {
    if (store == NULL || bp == NULL) {
        return 0;
    }
    if (store->backup_count >= MAX_BACKUP_PROFILES) {
        return 0;
    }
    /* Validate required fields. */
    if (bp->name[0] == L'\0' || bp->tree_root[0] == L'\0') {
        return 0;
    }
    /* Validate that the parent game profile exists. */
    bool parent_found = false;
    for (size_t i = 0; i < store->game_count; i++) {
        if (store->games[i].id == bp->parent_game_id) {
            parent_found = true;
            break;
        }
    }
    if (!parent_found) {
        return 0;
    }

    /* Create backup directory if it does not already exist. */
    SHCreateDirectoryExW(NULL, bp->tree_root, NULL);
    /* Ignore return value — ERROR_ALREADY_EXISTS is acceptable. */

    int id = store->next_backup_id++;
    store->backups[store->backup_count] = *bp;
    store->backups[store->backup_count].id = id;
    store->backup_count++;
    return id;
}

bool profile_store_update_backup(profile_store_t *store, int id, const backup_profile_t *bp) {
    if (store == NULL || bp == NULL || id <= 0) {
        return false;
    }
    for (size_t i = 0; i < store->backup_count; i++) {
        if (store->backups[i].id == id) {
            store->backups[i] = *bp;
            store->backups[i].id = id; /* preserve the original ID */
            return true;
        }
    }
    return false;
}

bool profile_store_delete_backup(profile_store_t *store, int id) {
    if (store == NULL || id <= 0) {
        return false;
    }
    for (size_t i = 0; i < store->backup_count; i++) {
        if (store->backups[i].id == id) {
            for (size_t k = i; k + 1 < store->backup_count; k++) {
                store->backups[k] = store->backups[k + 1];
            }
            store->backup_count--;
            if (store->active_backup_id == id) {
                store->active_backup_id = store->backup_count > 0 ? store->backups[0].id : 0;
            }
            return true;
        }
    }
    return false;
}

/* ==== Active selection ==== */

bool profile_store_set_active_game(profile_store_t *store, int id) {
    if (store == NULL || id <= 0) {
        return false;
    }
    for (size_t i = 0; i < store->game_count; i++) {
        if (store->games[i].id == id) {
            store->active_game_id = id;
            return true;
        }
    }
    return false;
}

bool profile_store_set_active_backup(profile_store_t *store, int id) {
    if (store == NULL || id <= 0) {
        return false;
    }
    for (size_t i = 0; i < store->backup_count; i++) {
        if (store->backups[i].id == id) {
            store->active_backup_id = id;
            return true;
        }
    }
    return false;
}

/* ==== List helpers ==== */

size_t profile_store_list_backups_for_game(const profile_store_t *store, int game_id,
                                           const backup_profile_t **out, size_t out_cap) {
    if (store == NULL) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < store->backup_count; i++) {
        if (store->backups[i].parent_game_id == game_id) {
            if (out != NULL && count < out_cap) {
                out[count] = &store->backups[i];
            }
            count++;
        }
    }
    return count;
}

/* ==== Load: parsing state and callbacks ==== */

/**
 * @brief Parsing context threaded through the section and kv callbacks during load.
 */
typedef struct parse_ctx_s {
    profile_store_t *store;
    int current_section_type;   /* 0=unknown, 1=settings, 2=game_profile, 3=backup_profile */
    int current_section_id;     /* The N parsed from [GameProfile:N] or [BackupProfile:N] */
    game_profile_t   cur_game;  /* Accumulator for the current GameProfile section */
    backup_profile_t cur_backup; /* Accumulator for the current BackupProfile section */
} parse_ctx_t;

/**
 * @brief Commit the pending profile into the store, then identify the new section.
 */
static void load_section_cb(const char *section, void *user) {
    parse_ctx_t *ctx = (parse_ctx_t *)user;

    /* Commit the previous section's accumulated data before switching. */
    if (ctx->current_section_type == 2 && ctx->current_section_id > 0) {
        if (ctx->store->game_count < MAX_GAME_PROFILES) {
            ctx->cur_game.id = ctx->current_section_id;
            ctx->store->games[ctx->store->game_count++] = ctx->cur_game;
        }
    } else if (ctx->current_section_type == 3 && ctx->current_section_id > 0) {
        if (ctx->store->backup_count < MAX_BACKUP_PROFILES) {
            ctx->cur_backup.id = ctx->current_section_id;
            ctx->store->backups[ctx->store->backup_count++] = ctx->cur_backup;
        }
    }

    ZeroMemory(&ctx->cur_game, sizeof(ctx->cur_game));
    ZeroMemory(&ctx->cur_backup, sizeof(ctx->cur_backup));
    ctx->current_section_id = 0;

    /* Classify the incoming section name. */
    if (strcmp(section, "Settings") == 0) {
        ctx->current_section_type = 1;
    } else if (strncmp(section, "GameProfile:", 12) == 0) {
        ctx->current_section_type = 2;
        ctx->current_section_id = atoi(section + 12);
    } else if (strncmp(section, "BackupProfile:", 14) == 0) {
        ctx->current_section_type = 3;
        ctx->current_section_id = atoi(section + 14);
    } else {
        ctx->current_section_type = 0;
    }
}

/**
 * @brief Dispatch a key=value pair to the correct accumulator.
 */
static void load_kv_cb(const char *key, const char *value, void *user) {
    parse_ctx_t *ctx = (parse_ctx_t *)user;

    if (ctx->current_section_type == 1) {
        /* [Settings] — only profile-store-owned keys. */
        if (strcmp(key, "ActiveGameProfileId") == 0) {
            ctx->store->active_game_id = atoi(value);
        } else if (strcmp(key, "ActiveBackupProfileId") == 0) {
            ctx->store->active_backup_id = atoi(value);
        }
        /* Other Settings keys (TreeRoot, Language, etc.) are owned by praxis_load_config. */
    } else if (ctx->current_section_type == 2) {
        /* [GameProfile:N] */
        if (strcmp(key, "Name") == 0) {
            config_core_store_wide_value(ctx->cur_game.name, 64, value);
        } else if (strcmp(key, "GameId") == 0) {
            ctx->cur_game.game_id = (game_id_t)atoi(value);
        } else if (strcmp(key, "OriginalSaveDir") == 0) {
            config_core_store_wide_value(ctx->cur_game.original_save_dir, MAX_PATH, value);
        } else if (strcmp(key, "TreeRoot") == 0) {
            config_core_store_wide_value(ctx->cur_game.tree_root, MAX_PATH, value);
        }
    } else if (ctx->current_section_type == 3) {
        /* [BackupProfile:N] */
        if (strcmp(key, "ParentGameId") == 0) {
            ctx->cur_backup.parent_game_id = atoi(value);
        } else if (strcmp(key, "Name") == 0) {
            config_core_store_wide_value(ctx->cur_backup.name, 64, value);
        } else if (strcmp(key, "TreeRoot") == 0) {
            config_core_store_wide_value(ctx->cur_backup.tree_root, MAX_PATH, value);
        } else if (strcmp(key, "CompressionLevel") == 0) {
            if (strcmp(value, "none") == 0) {
                ctx->cur_backup.compression_level = COMP_LEVEL_NONE;
            } else if (strcmp(value, "high") == 0) {
                ctx->cur_backup.compression_level = COMP_LEVEL_HIGH;
            } else {
                ctx->cur_backup.compression_level = COMP_LEVEL_LOW; /* default */
            }
        }
    }
}

/* ==== Load ==== */

bool profile_store_load(profile_store_t *store, const wchar_t *ini_path) {
    if (store == NULL || ini_path == NULL) {
        return false;
    }

    profile_store_init(store);

    HANDLE fh = CreateFileW(ini_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) {
        return true; /* File not found is OK (fresh install, empty store). */
    }

    DWORD file_size = GetFileSize(fh, NULL);
    if (file_size == INVALID_FILE_SIZE || file_size == 0 || file_size > PROFILE_STORE_MAX_BYTES) {
        CloseHandle(fh);
        return true; /* Empty or oversized file: treat as fresh store. */
    }

    char *buf = (char *)LocalAlloc(LMEM_FIXED, file_size + 1u);
    if (buf == NULL) {
        CloseHandle(fh);
        return false;
    }

    DWORD bytes_read = 0;
    if (!ReadFile(fh, buf, file_size, &bytes_read, NULL)) {
        LocalFree(buf);
        CloseHandle(fh);
        return false;
    }
    CloseHandle(fh);
    buf[bytes_read] = '\0';

    parse_ctx_t ctx;
    ZeroMemory(&ctx, sizeof(ctx));
    ctx.store = store;

    config_core_parse_ini_ex(buf, (size_t)bytes_read, load_section_cb, load_kv_cb, &ctx);

    /* Flush the last pending section (no following section header triggers the commit). */
    load_section_cb("", &ctx);

    LocalFree(buf);

    /* Orphan cleanup: remove backups whose parent game no longer exists. */
    size_t i = 0;
    while (i < store->backup_count) {
        bool found = false;
        for (size_t j = 0; j < store->game_count; j++) {
            if (store->games[j].id == store->backups[i].parent_game_id) {
                found = true;
                break;
            }
        }
        if (!found) {
            fwprintf(stderr, L"praxis: orphan BackupProfile:%d parent=%d (skipped)\n",
                     store->backups[i].id, store->backups[i].parent_game_id);
            for (size_t k = i; k + 1 < store->backup_count; k++) {
                store->backups[k] = store->backups[k + 1];
            }
            store->backup_count--;
        } else {
            i++;
        }
    }

    /* Active game ID fallback: if the active game is gone, pick the first available. */
    if (store->active_game_id != 0) {
        bool found = false;
        for (size_t j = 0; j < store->game_count; j++) {
            if (store->games[j].id == store->active_game_id) {
                found = true;
                break;
            }
        }
        if (!found) {
            store->active_game_id = store->game_count > 0 ? store->games[0].id : 0;
        }
    }

    /* Active backup ID fallback: if the active backup is gone, pick the first available. */
    if (store->active_backup_id != 0) {
        bool found = false;
        for (size_t j = 0; j < store->backup_count; j++) {
            if (store->backups[j].id == store->active_backup_id) {
                found = true;
                break;
            }
        }
        if (!found) {
            store->active_backup_id = store->backup_count > 0 ? store->backups[0].id : 0;
        }
    }

    /* Recompute next_game_id as max(all game IDs) + 1. */
    int max_game_id = 0;
    for (size_t j = 0; j < store->game_count; j++) {
        if (store->games[j].id > max_game_id) {
            max_game_id = store->games[j].id;
        }
    }
    store->next_game_id = max_game_id + 1;

    /* Recompute next_backup_id as max(all backup IDs) + 1. */
    int max_backup_id = 0;
    for (size_t j = 0; j < store->backup_count; j++) {
        if (store->backups[j].id > max_backup_id) {
            max_backup_id = store->backups[j].id;
        }
    }
    store->next_backup_id = max_backup_id + 1;

    return true;
}

/* ==== Save ==== */

bool profile_store_save(const profile_store_t *store, const wchar_t *ini_path) {
    if (store == NULL || ini_path == NULL) {
        return false;
    }

    /* Build the temporary file path by appending ".tmp". */
    wchar_t tmp_path[MAX_PATH];
    _snwprintf(tmp_path, MAX_PATH, L"%ls.tmp", ini_path);
    tmp_path[MAX_PATH - 1] = L'\0';

    /* Determine legacy TreeRoot: prefer active backup's tree_root for backward compat. */
    const backup_profile_t *active_bp = profile_store_get_active_backup(store);

    char tree_root_utf8[MAX_PATH * 4] = {0};
    if (active_bp != NULL && active_bp->tree_root[0] != L'\0') {
        WideCharToMultiByte(CP_UTF8, 0, active_bp->tree_root, -1,
                            tree_root_utf8, (int)sizeof(tree_root_utf8), NULL, NULL);
    } else {
        WideCharToMultiByte(CP_UTF8, 0, praxis_config.tree_root, -1,
                            tree_root_utf8, (int)sizeof(tree_root_utf8), NULL, NULL);
    }

    /* Map backup compression level to legacy integer (none→1, low→5, high→9). */
    int legacy_comp;
    if (active_bp != NULL) {
        switch (active_bp->compression_level) {
        case COMP_LEVEL_NONE: legacy_comp = 1; break;
        case COMP_LEVEL_HIGH: legacy_comp = 9; break;
        default:              legacy_comp = 5; break; /* COMP_LEVEL_LOW */
        }
    } else {
        legacy_comp = praxis_config.compression_level;
        if (legacy_comp <= 0) {
            legacy_comp = 5; /* safe default if praxis_config was not initialized */
        }
    }

    /* Convert hotkey strings from wide to UTF-8. */
    char hotkey_bf[32 * 4]  = {0};
    char hotkey_bs[32 * 4]  = {0};
    char hotkey_r[32 * 4]   = {0};
    char hotkey_ur[32 * 4]  = {0};
    WideCharToMultiByte(CP_UTF8, 0, praxis_config.hotkey_backup_full, -1,
                        hotkey_bf, (int)sizeof(hotkey_bf), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, praxis_config.hotkey_backup_slot, -1,
                        hotkey_bs, (int)sizeof(hotkey_bs), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, praxis_config.hotkey_restore, -1,
                        hotkey_r, (int)sizeof(hotkey_r), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, praxis_config.hotkey_undo_restore, -1,
                        hotkey_ur, (int)sizeof(hotkey_ur), NULL, NULL);

    config_core_buf_t buf;
    config_core_buf_init(&buf);

    /* [Settings] section: all praxis_config fields plus profile store selections. */
    config_core_buf_append(&buf, "[Settings]\r\n");
    config_core_buf_append(&buf, "TreeRoot=%s\r\n", tree_root_utf8);
    config_core_buf_append(&buf, "Language=%d\r\n", praxis_config.language);
    config_core_buf_append(&buf, "WindowX=%d\r\n", praxis_config.window_x);
    config_core_buf_append(&buf, "WindowY=%d\r\n", praxis_config.window_y);
    config_core_buf_append(&buf, "WindowWidth=%d\r\n", praxis_config.window_width);
    config_core_buf_append(&buf, "WindowHeight=%d\r\n", praxis_config.window_height);
    config_core_buf_append(&buf, "CompressionLevel=%d\r\n", legacy_comp);
    config_core_buf_append(&buf, "RingSize=%d\r\n", praxis_config.ring_size);
    config_core_buf_append(&buf, "HotkeyBackupFull=%s\r\n", hotkey_bf);
    config_core_buf_append(&buf, "HotkeyBackupSlot=%s\r\n", hotkey_bs);
    config_core_buf_append(&buf, "HotkeyRestore=%s\r\n", hotkey_r);
    config_core_buf_append(&buf, "HotkeyUndoRestore=%s\r\n", hotkey_ur);
    config_core_buf_append(&buf, "ActiveGameProfileId=%d\r\n", store->active_game_id);
    config_core_buf_append(&buf, "ActiveBackupProfileId=%d\r\n", store->active_backup_id);
    config_core_buf_append(&buf, "\r\n");

    /* [GameProfile:N] sections. */
    for (size_t gi = 0; gi < store->game_count; gi++) {
        const game_profile_t *gp = &store->games[gi];
        char name_utf8[64 * 4]          = {0};
        char orig_dir_utf8[MAX_PATH * 4] = {0};
        char game_tree_utf8[MAX_PATH * 4] = {0};

        WideCharToMultiByte(CP_UTF8, 0, gp->name, -1,
                            name_utf8, (int)sizeof(name_utf8), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, gp->original_save_dir, -1,
                            orig_dir_utf8, (int)sizeof(orig_dir_utf8), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, gp->tree_root, -1,
                            game_tree_utf8, (int)sizeof(game_tree_utf8), NULL, NULL);

        config_core_buf_append(&buf, "[GameProfile:%d]\r\n", gp->id);
        config_core_buf_append(&buf, "Name=%s\r\n", name_utf8);
        config_core_buf_append(&buf, "GameId=%d\r\n", (int)gp->game_id);
        config_core_buf_append(&buf, "OriginalSaveDir=%s\r\n", orig_dir_utf8);
        config_core_buf_append(&buf, "TreeRoot=%s\r\n", game_tree_utf8);
        config_core_buf_append(&buf, "\r\n");
    }

    /* [BackupProfile:N] sections. */
    for (size_t bi = 0; bi < store->backup_count; bi++) {
        const backup_profile_t *bp = &store->backups[bi];
        char name_utf8[64 * 4]         = {0};
        char bp_tree_utf8[MAX_PATH * 4] = {0};
        const char *comp_str;

        WideCharToMultiByte(CP_UTF8, 0, bp->name, -1,
                            name_utf8, (int)sizeof(name_utf8), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, bp->tree_root, -1,
                            bp_tree_utf8, (int)sizeof(bp_tree_utf8), NULL, NULL);

        switch (bp->compression_level) {
        case COMP_LEVEL_NONE: comp_str = "none"; break;
        case COMP_LEVEL_HIGH: comp_str = "high"; break;
        default:              comp_str = "low";  break;
        }

        config_core_buf_append(&buf, "[BackupProfile:%d]\r\n", bp->id);
        config_core_buf_append(&buf, "ParentGameId=%d\r\n", bp->parent_game_id);
        config_core_buf_append(&buf, "Name=%s\r\n", name_utf8);
        config_core_buf_append(&buf, "TreeRoot=%s\r\n", bp_tree_utf8);
        config_core_buf_append(&buf, "CompressionLevel=%s\r\n", comp_str);
        config_core_buf_append(&buf, "\r\n");
    }

    /* Write to the temp file. */
    if (!config_core_buf_write_file(&buf, tmp_path)) {
        config_core_buf_free(&buf);
        return false;
    }
    config_core_buf_free(&buf);

    /* Atomic replace: rename temp → final path. */
    if (!MoveFileExW(tmp_path, ini_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp_path);
        return false;
    }

    return true;
}
