/**
 * @file config.h
 * @brief Configuration management header file
 * @details This file contains declarations for configuration management functions
 *          and structures used in the Elden Ring face data manager.
 */

#pragma once

#include <wchar.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#include <windows.h>

/**
 * @brief Configuration structure for storing application settings
 * @details This structure holds all configurable parameters for the application,
 *          including save paths, language settings, and window properties.
 */
typedef struct {
    wchar_t save_path[MAX_PATH];      /* Path to save game data */
    wchar_t save_subfolder[32];       /* Subfolder name for save files */
    int language;                     /* Current language index (0-based) */
    int window_x;                     /* Window X position */
    int window_y;                     /* Window Y position */
    int window_width;                 /* Window width */
    int window_height;                /* Window height */
    int compression_level;            /* LZMA compression level 0..9; Fast=1, Normal=5, Max=9. Default 5. */
    int theme;                        /* Theme mode: 0=System, 1=Light, 2=Dark. Default 0. */
} config_t;

/**
 * @brief Global configuration variable
 * @details This variable holds the current application configuration
 *          and is accessible throughout the program.
 */
extern config_t config;

/**
 * @brief Load configuration from INI file
 * @details This function reads the configuration from the INI file
 *          and initializes the global config variable.
 */
extern void load_config(void);

/**
 * @brief Save configuration to INI file
 * @details This function writes the current configuration to the INI file.
 */
extern void save_config(void);
