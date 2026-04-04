/**
 * @file config.c
 * @brief Configuration management implementation
 * @details This file contains the implementation of configuration management
 *          functions for the Elden Ring face data manager.
 */

#include "config.h"

#include "locale.h"

#include <ini.h>

#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>

#include <stdio.h>

/* INI file configuration constants */
#define CONFIG_FILE L"ERSaveManager.ini"          /* Configuration file name */
#define CONFIG_SECTION L"Settings"                /* Main section name */
#define CONFIG_SAVE_PATH L"SavePath"              /* Save path key */
#define CONFIG_SAVE_SUBFOLDER L"SaveSubFolder"    /* Save subfolder key */
#define CONFIG_LANGUAGE L"Language"               /* Language setting key */
#define CONFIG_WINDOW_X L"WindowX"                /* Window X position key */
#define CONFIG_WINDOW_Y L"WindowY"                /* Window Y position key */
#define CONFIG_WINDOW_WIDTH L"WindowWidth"        /* Window width key */
#define CONFIG_WINDOW_HEIGHT L"WindowHeight"      /* Window height key */

/* Global configuration variable */
config_t config = {0};

/* Main window handle - defined in main.c */
extern HWND main_window;

/* inih callback function for parsing INI file */
static int my_ini_handler(void* user, const char* section, const char* name, const char* value) {
    config_t* cfg = (config_t*)user;

    /* Convert UTF-8 string to wide characters */
    wchar_t wsection[32];
    MultiByteToWideChar(CP_UTF8, 0, section, -1, wsection, 32);

    /* Check if it's our configuration section */
    if (lstrcmpW(wsection, CONFIG_SECTION) == 0) {
        wchar_t wname[32], wvalue[256];
        MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 32);
        MultiByteToWideChar(CP_UTF8, 0, value, -1, wvalue, 256);
        if (lstrcmpW(wname, CONFIG_SAVE_PATH) == 0) {
            lstrcpyW(cfg->save_path, wvalue);
            return 1;
        } else if (lstrcmpW(wname, CONFIG_SAVE_SUBFOLDER) == 0) {
            lstrcpyW(cfg->save_subfolder, wvalue);
            return 1;
        } else if (lstrcmpW(wname, CONFIG_LANGUAGE) == 0) {
            cfg->language = StrToIntW(wvalue);
            return 1;
        } else if (lstrcmpW(wname, CONFIG_WINDOW_X) == 0) {
            cfg->window_x = StrToIntW(wvalue);
            return 1;
        } else if (lstrcmpW(wname, CONFIG_WINDOW_Y) == 0) {
            cfg->window_y = StrToIntW(wvalue);
            return 1;
        } else if (lstrcmpW(wname, CONFIG_WINDOW_WIDTH) == 0) {
            cfg->window_width = StrToIntW(wvalue);
            return 1;
        } else if (lstrcmpW(wname, CONFIG_WINDOW_HEIGHT) == 0) {
            cfg->window_height = StrToIntW(wvalue);
            return 1;
        }
    }

    return 1; /* Return 1 indicating success */
}

/* Convert wide characters to UTF-8 */
static void wide_to_utf8(const wchar_t* wide_str, char *utf8_str, int len) {
    WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, utf8_str, len, NULL, NULL);
}

/* Convert UTF-8 to wide characters */
static void utf8_to_wide(const char* utf8_str, wchar_t* wide_str, int len) {
    MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, wide_str, len);
}

/* Write to INI file */
static void write_ini_file(const wchar_t* filename) {
    FILE* file = _wfopen(filename, L"w");

    if (file) {
        char utf8_section[32];
        char utf8_key[32];
        char utf8_value[512];

        /* Write section name */
        wide_to_utf8(CONFIG_SECTION, utf8_section, 32);
        fprintf(file, "[%s]\n", utf8_section);

        /* Write save path */
        wide_to_utf8(config.save_path, utf8_value, 512);
        wide_to_utf8(CONFIG_SAVE_PATH, utf8_key, 32);
        fprintf(file, "%s=%s\n", utf8_key, utf8_value);

        /* Write save subfolder */
        wide_to_utf8(config.save_subfolder, utf8_value, 512);
        wide_to_utf8(CONFIG_SAVE_SUBFOLDER, utf8_key, 32);
        fprintf(file, "%s=%s\n", utf8_key, utf8_value);

        /* Write language */
        wide_to_utf8(CONFIG_LANGUAGE, utf8_key, 32);
        fprintf(file, "%s=%d\n", utf8_key, config.language);

        /* Write window position and size */
        wide_to_utf8(CONFIG_WINDOW_X, utf8_key, 32);
        fprintf(file, "%s=%d\n", utf8_key, config.window_x);

        wide_to_utf8(CONFIG_WINDOW_Y, utf8_key, 32);
        fprintf(file, "%s=%d\n", utf8_key, config.window_y);

        wide_to_utf8(CONFIG_WINDOW_WIDTH, utf8_key, 32);
        fprintf(file, "%s=%d\n", utf8_key, config.window_width);

        wide_to_utf8(CONFIG_WINDOW_HEIGHT, utf8_key, 32);
        fprintf(file, "%s=%d\n", utf8_key, config.window_height);

        fclose(file);
    }
}

/**
 * @brief Load configuration from INI file
 * @details This function initializes the configuration structure,
 *          attempts to load settings from the INI file, and sets
 *          default values if the file doesn't exist.
 */
void load_config(void) {
    /* Get path to configuration file */
    wchar_t config_path[MAX_PATH];
    GetModuleFileNameW(NULL, config_path, MAX_PATH);
    PathRemoveFileSpecW(config_path);
    PathAppendW(config_path, CONFIG_FILE);

    /* Initialize configuration structure with default values */
    memset(&config, 0, sizeof(config_t));
    config.language = detect_system_language();
    config.window_x = -1;
    config.window_y = -1;
    config.window_width = 0;
    config.window_height = 0;

    /* Try to open and parse existing config file */
    FILE *file = _wfopen(config_path, L"r");
    if (file) {
        /* Parse INI file */
        ini_parse_file(file, my_ini_handler, &config);
        fclose(file);
    }

    /* If save path is empty, use default AppData path */
    if (config.save_path[0] == L'\0') {
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, config.save_path))) {
            PathAppendW(config.save_path, L"\\EldenRing");
        }
    }

    /* Set current locale based on configuration */
    set_current_locale(config.language);
}

/**
 * @brief Save configuration to INI file
 * @details This function writes the current configuration
 *          to the INI file.
 */
void save_config(void) {
    wchar_t config_path[MAX_PATH];
    GetModuleFileNameW(NULL, config_path, MAX_PATH);
    PathRemoveFileSpecW(config_path);
    PathAppendW(config_path, CONFIG_FILE);

    config.language = get_current_locale();

    /* If main window exists, get its position and size */
    if (main_window) {
        RECT rc;
        GetWindowRect(main_window, &rc);
        config.window_x = rc.left;
        config.window_y = rc.top;
        config.window_width = rc.right - rc.left;
        config.window_height = rc.bottom - rc.top;
    }

    /* Write INI file */
    write_ini_file(config_path);
}
