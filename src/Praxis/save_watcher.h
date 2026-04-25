/**
 * @file save_watcher.h
 * @brief ReadDirectoryChangesW worker-thread watcher with debounce.
 */

#pragma once

#include <stdbool.h>

#include <windows.h>

typedef struct save_watcher_s save_watcher_t;

/**
 * @brief Start watching a directory.
 * @details Posts message_id to notify_hwnd when the watched directory changes.
 * @param notify_hwnd HWND to receive PostMessageW notifications.
 * @param root_path Directory path to watch.
 * @param message_id Message to post on file change.
 * @return Allocated watcher on success, NULL on failure.
 */
save_watcher_t *save_watcher_start(HWND notify_hwnd, const wchar_t *root_path, UINT message_id);

/**
 * @brief Stop the watcher and free all resources.
 * @details Signals shutdown and waits up to 5 seconds for the worker thread.
 *          Uses TerminateThread only as a last resort.
 * @param w Watcher instance to stop.
 */
void save_watcher_stop(save_watcher_t *w);

/**
 * @brief Retarget the watcher to a new directory path.
 * @details Thread-safe. The worker thread re-opens the handle on wakeup.
 * @param w Watcher instance.
 * @param new_root New directory root to watch.
 * @return true on success, false on invalid input.
 */
bool save_watcher_change_root(save_watcher_t *w, const wchar_t *new_root);
