/**
 * @file ui_controls.h
 * @brief Main window UI creation, layout, and language refresh
 * @details Provides functions for creating main window controls,
 *          performing layout on resize, refreshing UI text after a
 *          language change, and releasing shared UI resources.
 */

#pragma once

#include <windows.h>

/**
 * @brief Create all main window controls
 * @param hwnd Main window handle
 * @param module Application module handle
 */
void ui_create_controls(HWND hwnd, HMODULE module);

/**
 * @brief Lay out controls to fit the current window size
 * @param hwnd Main window handle
 * @param width Current client area width
 * @param height Current client area height
 */
void ui_layout_controls(HWND hwnd, int width, int height);

/**
 * @brief Refresh all UI strings after a locale change
 * @details Updates window title, button labels, ListView columns,
 *          stat labels, character list items, and NPC face data menu.
 */
void ui_refresh_language(void);

/**
 * @brief Update enabled state of character action buttons
 * @details Enables/disables Import, Export, Rename buttons based on
 *          the current ListView selection and character data availability.
 */
void ui_update_char_buttons(void);

/**
 * @brief Release UI resources (font)
 */
void ui_cleanup(void);
