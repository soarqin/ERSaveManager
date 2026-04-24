/**
 * @file backend_registry.c
 * @brief Static backend registry implementation.
 */

#include "backend_registry.h"

/* Forward declaration — defined in backends/er_backend.c */
extern const game_backend_t er_backend;

static const game_backend_t *const g_backends[] = {
    &er_backend,
    NULL
};

const game_backend_t *backend_registry_get_by_id(game_id_t id) {
    for (size_t i = 0; g_backends[i] != NULL; i++) {
        if (g_backends[i]->id == id) return g_backends[i];
    }

    return NULL;
}

const game_backend_t *backend_registry_get_default(void) {
    return g_backends[0];
}

size_t backend_registry_count(void) {
    size_t n = 0;
    while (g_backends[n] != NULL) n++;
    return n;
}

const game_backend_t *backend_registry_get_at(size_t index) {
    size_t n = 0;
    while (g_backends[n] != NULL) n++;
    if (index >= n) return NULL;
    return g_backends[index];
}
