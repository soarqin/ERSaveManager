#pragma once
/**
 * @file save_tree_notify.h
 * @brief Save tree WM_NOTIFY handler.
 */

#include <windows.h>

#include "save_tree.h"

/**
 * @brief Handle WM_NOTIFY for the save tree control.
 * @param t Save tree instance.
 * @param nmhdr Notification header from WM_NOTIFY lParam.
 * @param result Output LRESULT.
 * @return true if handled.
 */
bool save_tree_notify_handle(save_tree_t *t, NMHDR *nmhdr, LRESULT *result);
