/**
 * @file backend_registry.h
 * @brief Static compile-time registry of game backends.
 */

#pragma once

#include "game_backend.h"

#include <stddef.h>

const game_backend_t *backend_registry_get_by_id(game_id_t id);
const game_backend_t *backend_registry_get_default(void);
size_t backend_registry_count(void);
const game_backend_t *backend_registry_get_at(size_t index);
