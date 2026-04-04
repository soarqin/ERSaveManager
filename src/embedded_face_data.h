/**
 * @file embedded_face_data.h
 * @brief Header file for embedded NPC face data presets
 * @details This file contains declarations for built-in NPC face data presets
 *          that can be imported into Elden Ring save files.
 */

#pragma once

#include <wchar.h>

/**
 * @brief Structure for an embedded NPC face data preset
 * @details Each preset contains localized names and raw face data bytes.
 */
typedef struct {
    int category;                      /* NPC category (0=base, 1=base non-interact, 2=DLC, 3=DLC non-interact) */
    const wchar_t* name[11];           /* Localized NPC names (one per supported locale) */
    const unsigned char data[0x130];   /* Raw face data bytes */
} embedded_face_data_t;

extern const embedded_face_data_t embedded_face_data[];
extern const int embedded_face_data_count;
