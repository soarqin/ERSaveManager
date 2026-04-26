/**
 * @file praxis_window_common.h
 * @brief Internal shared helpers for the Praxis main window.
 */

#pragma once

#include "game_backend.h"
#include "praxis_toast.h"
#include "profile_store.h"
#include "save_tree.h"
#include "save_watcher.h"
#include "toolbar.h"

#include <stdbool.h>

#include <windows.h>

typedef struct praxis_app_s {
    HWND main_window;
    save_tree_t *save_tree;
    save_watcher_t *save_watcher;
    HWND status_bar;
    toolbar_t *toolbar;
    praxis_toast_t *toast;
} praxis_app_t;

extern praxis_app_t g_app;
extern profile_store_t g_profile_store;
extern HANDLE g_log_file;

void log_write(const wchar_t *msg);
const game_backend_t *get_active_backend(void);
bool save_profile_store(void);
int get_active_compression_level(void);
void populate_toolbar_profiles(void);
void apply_active_profile_ui(HWND hwnd, UINT watcher_notify_msg);
void destroy_main_children(void);
void layout_main_window(WPARAM wp, LPARAM lp);
void register_hotkeys(HWND hwnd);
void handle_profile_combo_change(HWND hwnd, UINT watcher_notify_msg);
void handle_add_backup(HWND hwnd, UINT watcher_notify_msg);
void handle_delete_backup(HWND hwnd, UINT watcher_notify_msg);
void persist_window_placement(HWND hwnd);
