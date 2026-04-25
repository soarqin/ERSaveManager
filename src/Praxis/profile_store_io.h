/**
 * @file profile_store_io.h
 * @brief Profile store INI persistence (load/save).
 * @details Declares the two functions responsible for reading and writing
 *          the Praxis profile store to/from a Praxis.ini file.  All other
 *          profile_store_t operations (CRUD, accessors) live in profile_store.h.
 */

#pragma once

#include "profile_store.h"

#include <wchar.h>

/**
 * @brief Load profile store from INI file.
 * @param out_store Profile store to populate; always initialised before parsing.
 * @param ini_path  Path to Praxis.ini (wide string).
 * @return true on success (or benign miss like file-not-found), false on I/O error.
 */
bool profile_store_io_load(profile_store_t *out_store, const wchar_t *ini_path);

/**
 * @brief Save profile store to INI file.
 * @param store    Profile store to persist.
 * @param ini_path Path to Praxis.ini (wide string).
 * @return true on success, false on I/O error.
 */
bool profile_store_io_save(const profile_store_t *store, const wchar_t *ini_path);
