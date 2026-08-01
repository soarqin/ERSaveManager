/**
 * @file ersave.c
 * @brief Implementation of Elden Ring face data management functions
 * @details This file contains the implementation of functions for handling face data operations
 *          in Elden Ring save files, including loading, saving, importing and exporting face data.
 */

#include "ersave.h"

#include <md5.h>

#include <windows.h>
#include <shlwapi.h>

/* BND4 save file format constants */
/* Data buffer sizes */
#define ER_SUMMARY_DATA_SIZE         0x60000    /* Summary slot data buffer size */
#define ER_CHAR_DATA_SIZE            0x280000   /* Character slot data buffer size */
#define ER_PROFILE_SIZE              0x24C      /* Character profile data size */
#define ER_FACE_DATA_SIZE            0x130      /* Face data entry size */
#define ER_FILE_HEADER_SIZE          0x300      /* BND4 file header read size */
#define ER_SUMMARY_FACE_SECTION_SIZE 0x11D0     /* Size of face data section within summary */

/* Slot file sizes (data + 0x10 header prefix) */
#define ER_SUMMARY_SLOT_FILE_SIZE    0x60010    /* Summary slot: ER_SUMMARY_DATA_SIZE + header */
#define ER_CHAR_SLOT_FILE_SIZE       0x280010   /* Char slot: ER_CHAR_DATA_SIZE + header */
#define ER_SLOT_HEADER_SIZE          0x10       /* Slot header prefix size (MD5 hash) */

/* BND4 file header field offsets */
#define ER_HEADER_SLOT_COUNT_OFFSET  0x0C       /* Slot count field offset */
#define ER_HEADER_SLOT_SIZE_BASE     0x48       /* Base offset of slot size array */
#define ER_HEADER_SLOT_OFFSET_BASE   0x50       /* Base offset of slot offset array */
#define ER_HEADER_SLOT_STRIDE        0x20       /* Stride between slot entries */

/* parse_char_slot section sizes and counts */
#define ER_CHAR_INITIAL_OFFSET       0x20       /* Initial data offset in char buffer */
#define ER_ITEM_LIST_COUNT           0x1400     /* Number of item list entries */
#define ER_STATS_SECTION_SIZE        0x94       /* Stats section size */
#define ER_CHAR_NAME_SIZE            0x22       /* Character name field byte size */
#define ER_FACE_SECTION_SIZE         0x120      /* Face data section size within char data */
#define ER_MENU_PROFILE_SIZE         0x1008     /* Menu profile save/load section size */
#define ER_FLAGS_SIZE                0x1bf99f   /* Event flags section size */
#define ER_NET_DATA_SIZE             0x20000    /* Network data section size */

/* parse_character_info padding sizes */
#define ER_CHAR_POST_GIFT_SIZE          0x1E    /* Data section following the gift field */
#define ER_CHAR_POST_MATCHMAKING_SIZE   0x35    /* Data section following the matchmaking weapon level field */

/* Inventory entry counts */
#define ER_INV1_PART1_COUNT             0xA80   /* First inventory section: item slot entry count */
#define ER_INV1_PART2_COUNT             0x180   /* Second inventory section: item slot entry count */
#define ER_INV2_PART1_COUNT             0x780   /* Second inventory block, first section: entry count */

/* Item seen list */
#define ER_ITEM_SEEN_COUNT              7000    /* Item seen list entry count */
#define ER_ITEM_SEEN_ENTRY_SIZE         0x10    /* Item seen list entry size in bytes */

/* Trailing section sizes */
#define ER_TUTORIAL_DATA_SIZE           0x40B   /* Tutorial data section size */

/* Summary data parsing */
#define ER_SUMMARY_DATA_LEAD_SIZE       0x140   /* Lead section in summary data after user ID */

/* Character and regulation version metadata */
#define ER_CHAR_VERSION_OFFSET              0x00
#define ER_CHAR_TRAILING_VERSION_SIZE       0x10
#define ER_REGULATION_HEADER_SIZE           0x10
#define ER_REGULATION_MAGIC                 0x52454720u
#define ER_REGULATION_DATA_SIZE             0x240000
#define ER_REGULATION_SLOT_FILE_SIZE        0x240020

static const er_version_target_t version_targets[] = {
    { L"1.02",   69,  13  },
    { L"1.02.1", 70,  14  },
    { L"1.02.2", 71,  15  },
    { L"1.02.3", 72,  16  },
    { L"1.03",   80,  20  },
    { L"1.03.1", 80,  20  },
    { L"1.03.2", 81,  21  },
    { L"1.04",   90,  30  },
    { L"1.04.1", 91,  31  },
    { L"1.05",   100, 40  },
    { L"1.06",   110, 50  },
    { L"1.07",   120, 60  },
    { L"1.08",   130, 70  },
    { L"1.08.1", 131, 71  },
    { L"1.09",   140, 80  },
    { L"1.09.1", 141, 81  },
    { L"1.10",   150, 90  },
    { L"1.10.1", 151, 91  },
    { L"1.12",   210, 211 },
    { L"1.12.3", 211, 212 },
    { L"1.13",   220, 220 },
    { L"1.13.2", 220, 220 },
    { L"1.14",   230, 230 },
    { L"1.15",   240, 240 },
    { L"1.16",   250, 250 },
    { L"1.16.1", 251, 251 },
    { L"1.16.2", 252, 252 },
};

static const er_save_downgrade_target_t save_downgrade_targets[] = {
    { L"1.02",   L"1.02.1", 69,  13,  10210038, 1811088, {0xF6,0x96,0x05,0xBE,0x94,0x25,0x56,0xB1,0xE2,0xE6,0x98,0x40,0xD8,0x74,0x58,0x6F} },
    { L"1.02.1", L"1.02.1", 70,  14,  10210038, 1811088, {0xF6,0x96,0x05,0xBE,0x94,0x25,0x56,0xB1,0xE2,0xE6,0x98,0x40,0xD8,0x74,0x58,0x6F} },
    { L"1.02.2", L"1.02.1", 71,  15,  10210038, 1811088, {0xF6,0x96,0x05,0xBE,0x94,0x25,0x56,0xB1,0xE2,0xE6,0x98,0x40,0xD8,0x74,0x58,0x6F} },
    { L"1.02.3", L"1.02.1", 72,  16,  10210038, 1811088, {0xF6,0x96,0x05,0xBE,0x94,0x25,0x56,0xB1,0xE2,0xE6,0x98,0x40,0xD8,0x74,0x58,0x6F} },
    { L"1.03",   L"1.03.1", 80,  20,  10310059, 1829216, {0x3A,0x26,0x41,0xD0,0x49,0x13,0x33,0xCE,0x5D,0x28,0x31,0x0E,0xAC,0x2E,0x09,0x47} },
    { L"1.03.1", L"1.03.1", 80,  20,  10310059, 1829216, {0x3A,0x26,0x41,0xD0,0x49,0x13,0x33,0xCE,0x5D,0x28,0x31,0x0E,0xAC,0x2E,0x09,0x47} },
    { L"1.03.2", L"1.03.2", 81,  21,  10320064, 1829232, {0x70,0x32,0x8E,0x2A,0xE4,0xE4,0x87,0x82,0x28,0xD9,0x51,0xF7,0x13,0x66,0xC8,0x1B} },
    { L"1.03.2", L"1.03.3", 81,  21,  10330078, 1829232, {0x38,0x3F,0xDE,0xA8,0x39,0x75,0xFD,0x2B,0xFD,0x19,0xDB,0x2F,0x8D,0x91,0x58,0x95} },
    { L"1.04",   L"1.04.1", 90,  30,  10410090, 1831120, {0x4F,0x58,0x96,0x20,0x25,0x98,0x0B,0x94,0x3C,0xB5,0xC9,0x6D,0x63,0xD8,0xE1,0xF1} },
    { L"1.04.1", L"1.04.2", 91,  31,  10420097, 1831120, {0x7C,0x30,0x6C,0x8E,0x51,0x64,0xB8,0x92,0x43,0x38,0x21,0xE8,0x5E,0x9F,0x3F,0xD4} },
    { L"1.05",   L"1.05",   100, 40,  10501000, 1831600, {0xE7,0xF1,0x81,0x72,0x1D,0x98,0xB6,0xA6,0x22,0x78,0x6F,0xAD,0x54,0x87,0xDF,0x0C} },
    { L"1.06",   L"1.06",   110, 50,  10601000, 1834080, {0xFF,0xE8,0x29,0x02,0xFA,0x70,0x2F,0xA1,0x51,0x87,0xBC,0xCF,0xA8,0x57,0x0F,0x14} },
    { L"1.07",   L"1.07.1", 120, 60,  10710188, 1838496, {0x74,0x0E,0xDD,0x4E,0x0B,0x31,0x99,0x25,0xBC,0x2E,0xFA,0xC7,0xAF,0xA8,0x8B,0x25} },
    { L"1.07",   L"1.07",   120, 60,  10701000, 1838496, {0xE7,0x85,0xD4,0x1C,0xFD,0xCB,0x3C,0x10,0x17,0x22,0xF8,0xC9,0x2C,0x19,0xE2,0x5A} },
    { L"1.08",   L"1.08",   130, 70,  10801000, 1844112, {0x34,0x56,0x9B,0x99,0x17,0x2D,0x0C,0xE4,0xB0,0x77,0x30,0x27,0x09,0xCE,0x64,0x58} },
    { L"1.08.1", L"1.08.1", 131, 71,  10811000, 1844112, {0xAD,0x5B,0x77,0x0F,0x2D,0x3B,0xBD,0x7E,0x72,0xED,0x23,0xCC,0x9E,0x21,0xAF,0x76} },
    { L"1.09",   L"1.09",   140, 80,  10901000, 1852464, {0x87,0x36,0x55,0xD8,0xF1,0xD6,0xC0,0x28,0x2C,0x18,0x6D,0x20,0x2B,0xC8,0x91,0x88} },
    { L"1.09.1", L"1.09.1", 141, 81,  10911000, 1852608, {0xEA,0x55,0x76,0xE9,0xBF,0x67,0xC5,0x78,0xC0,0x10,0xA1,0xD8,0xC0,0x5E,0x7E,0x43} },
    { L"1.10",   L"1.10",   150, 90,  11001000, 1859440, {0x9D,0xE4,0x83,0x80,0x78,0xB2,0x28,0x9D,0x83,0x8D,0x28,0x7A,0x24,0x31,0xE6,0x45} },
    { L"1.10.1", L"1.10",   151, 91,  11001000, 1859440, {0x9D,0xE4,0x83,0x80,0x78,0xB2,0x28,0x9D,0x83,0x8D,0x28,0x7A,0x24,0x31,0xE6,0x45} },
    { L"1.12",   L"1.12.1", 210, 211, 11210015, 2006960, {0x46,0xDC,0xF5,0xDB,0x86,0x16,0x76,0xE6,0xA5,0xDD,0xB8,0x93,0x7A,0x04,0x84,0xE1} },
    { L"1.12",   L"1.12.2", 210, 211, 11220021, 2006960, {0x21,0xE6,0xE0,0x4D,0x0B,0x62,0xF0,0x31,0x97,0x62,0x72,0x0C,0x6D,0x9C,0x89,0x74} },
    { L"1.12.3", L"1.12.4", 211, 212, 11240023, 2001488, {0xF9,0xAE,0x59,0x98,0x92,0x64,0xEA,0x4D,0xBE,0x82,0x8C,0x61,0x41,0xE1,0x80,0x31} },
    { L"1.13",   L"1.13.1", 220, 220, 11310027, 2025152, {0x20,0xDE,0x73,0xC1,0x14,0x8B,0x23,0xF0,0x63,0x65,0x6F,0x31,0xDF,0x7D,0x21,0x13} },
    { L"1.13",   L"1.13.2", 220, 220, 11320031, 2019824, {0xA6,0xD0,0x9F,0x42,0x45,0x0F,0x84,0xB3,0x8B,0x67,0xEF,0x23,0x9E,0x8D,0x59,0x1C} },
    { L"1.14",   L"1.14.1", 230, 230, 11410033, 2036288, {0xCA,0x52,0xB5,0xBA,0x65,0x98,0x54,0x70,0x4D,0x66,0xE7,0x5B,0x4A,0x0D,0x00,0x52} },
    { L"1.15",   L"1.15",   240, 240, 11501000, 2036272, {0x1C,0x98,0x20,0x5A,0x71,0xCE,0xC4,0x5A,0xEE,0x7D,0xF4,0x47,0x30,0x98,0xB2,0x35} },
    { L"1.16",   L"1.16",   250, 250, 11601000, 2036272, {0x42,0xA1,0x04,0x6B,0xA1,0xAA,0x17,0xBE,0x58,0xCE,0x92,0x25,0xF0,0x3A,0xC5,0x3F} },
    { L"1.16.1", L"1.16.1", 251, 251, 11611000, 2036272, {0x56,0xEC,0x3B,0x35,0xBC,0x94,0x12,0xAA,0xC9,0x83,0x8F,0x39,0x9F,0x2F,0x8A,0xFD} },
};

/* Structure to hold summary data - Contains face data for all character slots */
typedef struct er_summary_data_s {
    uint8_t data[ER_SUMMARY_DATA_SIZE]; /* Raw summary data buffer - Stores face data for all slots */
    uint32_t slot_offset; /* Offset to summary slot in file - Used for file operations */
    uint32_t face_offset; /* Offset to face data in summary data buffer - Points to start of face data section */
    uint32_t available_offset; /* Offset to available slot in summary data buffer - Points to start of available slot data section */
    uint32_t profile_offset; /* Offset to profile data in summary data buffer - Points to start of profile data section */
    uint32_t active_offset; /* Offset to active slot in summary data buffer - active slot index */
} er_summary_data_t;

/* Structure to hold character data - Contains individual character slot information */
typedef struct er_char_data_s {
    uint32_t slot_offset; /* Offset to character data slot in file - Used for file operations */
    uint32_t userid_offset; /* Offset to userid in data buffer - Points to user identification data */
    uint32_t stats_offset; /* Offset to stats in data buffer - Points to character statistics */
    uint32_t face_offset; /* Offset to face data in data buffer - Points to character face data */
    uint32_t death_count_offset; /* Offset to death count in data buffer - Points to character death count data */
    uint8_t data[ER_CHAR_DATA_SIZE]; /* Raw character data buffer - Stores complete character data */
    uint8_t profile[ER_PROFILE_SIZE]; /* Raw profile data buffer - Stores profile data */
} er_char_data_t;

/* Structure to hold complete save data - Contains all character slots and summary data */
typedef struct er_save_data_s {
    wchar_t full_path[MAX_PATH]; /* Full path to save file - Stores the complete file path */
    er_char_data_t char_data[10]; /* Array of character data slots - Stores data for up to 10 characters */
    er_summary_data_t summary_data; /* Summary data structure - Contains face data for all slots */
    uint32_t regulation_header[4]; /* UserData011 header, or zeroes for a nonstandard entry */
    uint32_t regulation_slot_offset; /* Offset to UserData011 in the BND4 file */
    uint32_t regulation_slot_size; /* Total UserData011 entry size including MD5 */
} er_save_data_t;

/* Structure to hold simple save data - Contains offsets to all slots and summary data */
typedef struct er_save_simple_data_s {
    wchar_t full_path[MAX_PATH]; /* Full path to save file - Stores the complete file path */
    wchar_t char_name[10][32]; /* Character names */
    uint32_t slot_offset[10]; /* Offset to each character slot */
    uint32_t summary_slot_offset; /* Offset to summary slot */
    uint32_t summary_profile_offset; /* Offset to profile data in summary data buffer - Points to start of profile data section */
} er_save_simple_data_t;

/**
 * @brief Validates face data structure by checking magic numbers
 * @param face_data Pointer to face data structure to validate
 * @return true if face data is valid (has correct magic numbers), false otherwise
 */
static bool validate_face_data(const uint8_t *face_data) { return face_data && face_data[0x00] == 0x01 && RtlCompareMemory(face_data + 0x10, "FACE", 4) == 4; }

static bool path_fits_fixed_buffer(const wchar_t *path) {
    return path && (size_t)lstrlenW(path) < MAX_PATH;
}

static bool copy_name_field(uint8_t *field, size_t field_bytes, const wchar_t *name) {
    if (!field || !name || field_bytes < sizeof(wchar_t)) {
        return false;
    }

    ZeroMemory(field, field_bytes);
    return lstrcpynW((wchar_t *)field, name, (int)(field_bytes / sizeof(wchar_t))) != NULL;
}

/**
 * @brief Helper function to read a uint8 value from a buffer
 * @param ptr Pointer to the buffer position to read from
 * @return The read uint8 value
 */
static uint8_t read_uint8(const uint8_t **ptr) {
    uint8_t value = **ptr;
    *ptr += sizeof(uint8_t);
    return value;
}

/**
 * @brief Helper function to read a uint32 value from a buffer
 * @param ptr Pointer to the buffer position to read from
 * @return The read uint32 value
 */
static uint32_t read_uint32(const uint8_t **ptr) {
    uint32_t value = *(const uint32_t *)*ptr;
    *ptr += sizeof(uint32_t);
    return value;
}

/**
 * @brief Helper function to write data at a specific file offset
 * @param file Handle to the file
 * @param offset Byte offset from the beginning of the file
 * @param data Pointer to the data buffer to write
 * @param size Number of bytes to write
 * @return true if write successful, false otherwise
 */
static bool write_at(HANDLE file, uint32_t offset, const void *data, DWORD size) {
    if (SetFilePointer(file, offset, NULL, FILE_BEGIN) != offset) return false;
    DWORD written;
    if (!WriteFile(file, data, size, &written, NULL) || written != size) return false;
    return true;
}

static bool read_at(HANDLE file, uint32_t offset, void *data, DWORD size) {
    DWORD bytes_read;

    if (SetFilePointer(file, offset, NULL, FILE_BEGIN) != offset) {
        return false;
    }
    return ReadFile(file, data, size, &bytes_read, NULL) && bytes_read == size;
}

static bool file_region_matches(HANDLE file, uint32_t offset,
                                const uint8_t *expected, uint32_t size) {
    uint8_t *buffer = (uint8_t *)LocalAlloc(LMEM_FIXED, 0x10000);
    bool matches = buffer != NULL;

    for (uint32_t done = 0; matches && done < size;) {
        DWORD chunk = size - done > 0x10000 ? 0x10000 : size - done;
        matches = read_at(file, offset + done, buffer, chunk)
            && RtlCompareMemory(buffer, expected + done, chunk) == chunk;
        done += chunk;
    }
    if (buffer) {
        LocalFree(buffer);
    }
    return matches;
}

static bool save_snapshot_matches_file(const er_save_data_t *save_data, HANDLE file) {
    uint8_t header[ER_FILE_HEADER_SIZE];

    if (!read_at(file, 0, header, sizeof(header))
        || RtlCompareMemory(header, "BND4", 4) != 4
        || *(const int *)(header + ER_HEADER_SLOT_COUNT_OFFSET) < 12) {
        return false;
    }

    for (int i = 0; i < 10; i++) {
        if (*(const uint32_t *)(header + ER_HEADER_SLOT_SIZE_BASE + i * ER_HEADER_SLOT_STRIDE)
                != ER_CHAR_SLOT_FILE_SIZE
            || *(const uint32_t *)(header + ER_HEADER_SLOT_OFFSET_BASE + i * ER_HEADER_SLOT_STRIDE)
                != save_data->char_data[i].slot_offset) {
            return false;
        }
        if (save_data->summary_data.data[save_data->summary_data.available_offset + i]
            && !file_region_matches(file,
                save_data->char_data[i].slot_offset + ER_SLOT_HEADER_SIZE,
                save_data->char_data[i].data, ER_CHAR_DATA_SIZE)) {
            return false;
        }
    }

    return *(const uint32_t *)(header + ER_HEADER_SLOT_SIZE_BASE + 10 * ER_HEADER_SLOT_STRIDE)
                == ER_SUMMARY_SLOT_FILE_SIZE
        && *(const uint32_t *)(header + ER_HEADER_SLOT_OFFSET_BASE + 10 * ER_HEADER_SLOT_STRIDE)
                == save_data->summary_data.slot_offset
        && *(const uint32_t *)(header + ER_HEADER_SLOT_SIZE_BASE + 11 * ER_HEADER_SLOT_STRIDE)
                == save_data->regulation_slot_size
        && *(const uint32_t *)(header + ER_HEADER_SLOT_OFFSET_BASE + 11 * ER_HEADER_SLOT_STRIDE)
                == save_data->regulation_slot_offset
        && file_region_matches(file,
            save_data->summary_data.slot_offset + ER_SLOT_HEADER_SIZE,
            save_data->summary_data.data, ER_SUMMARY_DATA_SIZE)
        && file_region_matches(file,
            save_data->regulation_slot_offset + ER_SLOT_HEADER_SIZE,
            (const uint8_t *)save_data->regulation_header, ER_REGULATION_HEADER_SIZE);
}

static bool is_known_character_version(uint32_t version) {
    for (size_t i = 0; i < sizeof(version_targets) / sizeof(version_targets[0]); i++) {
        if (version_targets[i].character_version == version) {
            return true;
        }
    }
    return false;
}

static bool read_regulation_header(er_save_data_t *save_data, HANDLE file,
                                   uint32_t slot_offset, uint32_t slot_size) {
    DWORD bytes_read;

    ZeroMemory(save_data->regulation_header, sizeof(save_data->regulation_header));
    if (slot_size < ER_SLOT_HEADER_SIZE + ER_REGULATION_HEADER_SIZE
        || SetFilePointer(file, slot_offset + ER_SLOT_HEADER_SIZE, NULL, FILE_BEGIN)
            != slot_offset + ER_SLOT_HEADER_SIZE
        || !ReadFile(file, save_data->regulation_header, ER_REGULATION_HEADER_SIZE,
                     &bytes_read, NULL)
        || bytes_read != ER_REGULATION_HEADER_SIZE) {
        ZeroMemory(save_data->regulation_header, sizeof(save_data->regulation_header));
        return false;
    }
    return true;
}

static uint8_t *read_regulation_file(const wchar_t *path, DWORD *out_size) {
    LARGE_INTEGER size;
    uint8_t *data;
    HANDLE file;
    DWORD bytes_read;

    if (!path || !out_size) {
        return NULL;
    }

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 16
        || size.QuadPart > ER_REGULATION_DATA_SIZE || (size.QuadPart & 0xF) != 0) {
        CloseHandle(file);
        return NULL;
    }

    data = (uint8_t *)LocalAlloc(LMEM_FIXED, (SIZE_T)size.QuadPart);
    if (!data) {
        CloseHandle(file);
        return NULL;
    }
    if (!ReadFile(file, data, (DWORD)size.QuadPart, &bytes_read, NULL)
        || bytes_read != (DWORD)size.QuadPart) {
        LocalFree(data);
        CloseHandle(file);
        return NULL;
    }

    CloseHandle(file);
    *out_size = (DWORD)size.QuadPart;
    return data;
}

static bool downgrade_target_is_known(const er_save_downgrade_target_t *target) {
    for (size_t i = 0; i < er_save_downgrade_target_count(); i++) {
        if (target == er_save_downgrade_target_get(i)) {
            return true;
        }
    }
    return false;
}

static bool make_downgrade_temp_path(const wchar_t *save_path, wchar_t *temp_path) {
    wchar_t directory[MAX_PATH];
    wchar_t prefix[4] = L"erd";

    if (!save_path || !temp_path || (size_t)lstrlenW(save_path) >= MAX_PATH) {
        return false;
    }
    lstrcpyW(directory, save_path);
    if (!PathRemoveFileSpecW(directory)) {
        return false;
    }
    return GetTempFileNameW(directory, prefix, 0, temp_path) != 0;
}

static HANDLE make_verified_downgrade_temp_copy(const er_save_data_t *save_data,
                                                wchar_t *temp_path,
                                                HANDLE *source_file,
                                                uint8_t **source_snapshot,
                                                DWORD *source_size) {
    LARGE_INTEGER size;
    HANDLE file;

    temp_path[0] = L'\0';
    *source_snapshot = NULL;
    *source_size = 0;
    *source_file = CreateFileW(save_data->full_path, GENERIC_READ,
                               FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (*source_file == INVALID_HANDLE_VALUE
        || !save_snapshot_matches_file(save_data, *source_file)
        || !GetFileSizeEx(*source_file, &size)
        || size.QuadPart <= 0 || size.QuadPart > MAXDWORD
        || !make_downgrade_temp_path(save_data->full_path, temp_path)
        || !CopyFileW(save_data->full_path, temp_path, FALSE)) {
        if (*source_file != INVALID_HANDLE_VALUE) {
            CloseHandle(*source_file);
        }
        if (temp_path[0] != L'\0') {
            DeleteFileW(temp_path);
        }
        return INVALID_HANDLE_VALUE;
    }

    *source_size = (DWORD)size.QuadPart;
    *source_snapshot = (uint8_t *)LocalAlloc(LMEM_FIXED, *source_size);
    if (!*source_snapshot
        || !read_at(*source_file, 0, *source_snapshot, *source_size)) {
        if (*source_snapshot) {
            LocalFree(*source_snapshot);
            *source_snapshot = NULL;
        }
        CloseHandle(*source_file);
        DeleteFileW(temp_path);
        return INVALID_HANDLE_VALUE;
    }

    file = CreateFileW(temp_path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        LocalFree(*source_snapshot);
        *source_snapshot = NULL;
        CloseHandle(*source_file);
        DeleteFileW(temp_path);
        return INVALID_HANDLE_VALUE;
    }

    if (!save_snapshot_matches_file(save_data, file)) {
        CloseHandle(file);
        LocalFree(*source_snapshot);
        *source_snapshot = NULL;
        CloseHandle(*source_file);
        DeleteFileW(temp_path);
        file = INVALID_HANDLE_VALUE;
    }
    return file;
}

static bool restore_replacement_backup(const wchar_t *save_path,
                                       const wchar_t *backup_path) {
    wchar_t failed_path[MAX_PATH];

    if (GetFileAttributesW(backup_path) == INVALID_FILE_ATTRIBUTES
        || !make_downgrade_temp_path(save_path, failed_path)) {
        return false;
    }
    DeleteFileW(failed_path);
    if (!ReplaceFileW(save_path, backup_path, failed_path,
                      REPLACEFILE_WRITE_THROUGH, NULL, NULL)) {
        DeleteFileW(failed_path);
        return false;
    }
    DeleteFileW(failed_path);
    return true;
}

static bool commit_downgrade_temp_copy(const wchar_t *save_path,
                                       const wchar_t *temp_path, HANDLE file,
                                       HANDLE source_file,
                                       const uint8_t *source_snapshot,
                                       DWORD source_size) {
    bool ok = FlushFileBuffers(file) != 0;
    wchar_t backup_path[MAX_PATH];

    CloseHandle(file);
    if (!ok || !make_downgrade_temp_path(save_path, backup_path)) {
        CloseHandle(source_file);
        DeleteFileW(temp_path);
        return false;
    }
    DeleteFileW(backup_path);
    CloseHandle(source_file);
    ok = ReplaceFileW(save_path, temp_path, backup_path,
                      REPLACEFILE_WRITE_THROUGH, NULL, NULL) != 0;
    if (ok) {
        HANDLE backup_file = CreateFileW(backup_path, GENERIC_READ, FILE_SHARE_READ,
                                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        LARGE_INTEGER backup_size;
        bool backup_matches = backup_file != INVALID_HANDLE_VALUE
            && GetFileSizeEx(backup_file, &backup_size)
            && backup_size.QuadPart == source_size
            && file_region_matches(backup_file, 0, source_snapshot, source_size);
        if (backup_file != INVALID_HANDLE_VALUE) {
            CloseHandle(backup_file);
        }
        if (backup_matches) {
            DeleteFileW(backup_path);
        } else {
            restore_replacement_backup(save_path, backup_path);
            ok = false;
        }
    } else if (GetFileAttributesW(save_path) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(temp_path);
        DeleteFileW(backup_path);
    } else if (GetFileAttributesW(backup_path) != INVALID_FILE_ATTRIBUTES) {
        MoveFileExW(backup_path, save_path, MOVEFILE_WRITE_THROUGH);
        DeleteFileW(temp_path);
    } else if (GetFileAttributesW(temp_path) != INVALID_FILE_ATTRIBUTES) {
        MoveFileExW(temp_path, save_path, MOVEFILE_WRITE_THROUGH);
    }
    return ok;
}

static bool parse_item_list(er_char_data_t *char_data, const uint8_t **ptr, const uint8_t *end) {
    (void)char_data; /* no offset to record */
    for (size_t i = 0; i < ER_ITEM_LIST_COUNT; i++) {
        *ptr += 4;
        const uint32_t itemId = read_uint32(ptr);
        if (itemId == 0 || itemId == 0xFFFFFFFFu) continue;
        switch (itemId >> 28) {
            case 0:
                *ptr += 13;
                break;
            case 1:
                *ptr += 8;
                break;
            default:
                break;
        }
    }
    if (*ptr > end) return false;
    char_data->stats_offset = (uint32_t)(*ptr - char_data->data);
    return true;
}

static bool parse_character_info(er_char_data_t *char_data, const uint8_t **ptr, const uint8_t *end) {
    (void)char_data;
    static const size_t total_size = (size_t)ER_STATS_SECTION_SIZE + ER_CHAR_NAME_SIZE
        + 1 + 1 + 3 + 1 + ER_CHAR_POST_GIFT_SIZE + 1 + ER_CHAR_POST_MATCHMAKING_SIZE
        + 0x12 * 6 + 0x34 + 0xd0;
    if ((size_t)(end - *ptr) < total_size) return false;
    /* character data */
    *ptr += ER_STATS_SECTION_SIZE;
    /* charname */
    *ptr += ER_CHAR_NAME_SIZE;
    /* gender */
    *ptr += 1;
    /* birth job */
    *ptr += 1;
    *ptr += 3;
    /* gift */
    *ptr += 1;
    *ptr += ER_CHAR_POST_GIFT_SIZE;
    /* match making weapon level */
    *ptr += 1;
    *ptr += ER_CHAR_POST_MATCHMAKING_SIZE;
    /* passwords */
    *ptr += 0x12 * 6;
    *ptr += 0x34;

    *ptr += 0xd0;
    return true;
}

static bool parse_equipment(er_char_data_t *char_data, const uint8_t **ptr, const uint8_t *end) {
    (void)char_data;
    static const size_t total_size = 4 * 6 + 4 * 4 + 4 * 2 + 4 * 4 + 4 + 4 * 4 + 4 + 4 * 29 + 4 * 22;
    if ((size_t)(end - *ptr) < total_size) return false;
    /* equip data */
    /* weapons */
    *ptr += 4 * 6;
    /* arrows and bolts */
    *ptr += 4 * 4;
    *ptr += 4 * 2;
    /* armors */
    *ptr += 4 * 4;
    *ptr += 4;
    /* accessories */
    *ptr += 4 * 4;
    *ptr += 4;

    /* ChrAsm */
    *ptr += 4 * 29;

    /* ChrAsm2 */
    *ptr += 4 * 22;
    return true;
}

static bool parse_inventory(er_char_data_t *char_data, const uint8_t **ptr, const uint8_t *end) {
    (void)char_data;
    static const size_t total_size = 4 + (size_t)0x0C * ER_INV1_PART1_COUNT + 4
        + (size_t)0x0C * ER_INV1_PART2_COUNT + 4 + 4
        + 8 * 14 + 4 + 8 * 10 + 4 + 8 * 6 + 8 + 4 * 6;
    if ((size_t)(end - *ptr) < total_size) return false;
    /* inventory 1 */
    /* count 1 */
    *ptr += 4;
    /* part 1 */
    *ptr += 0x0C * ER_INV1_PART1_COUNT;
    /* count 2 */
    *ptr += 4;
    /* part 2 */
    *ptr += 0x0C * ER_INV1_PART2_COUNT;
    /* next_equip_index */
    *ptr += 4;
    /* next_acquisition_sort_id */
    *ptr += 4;

    /* spells */
    *ptr += 8 * 14;
    /* current spell slot */
    *ptr += 4;

    /* quick item slot: item_id + equipment_index */
    *ptr += 8 * 10;
    /* current quick slot */
    *ptr += 4;
    /* pouch item slot */
    *ptr += 8 * 6;
    *ptr += 8;

    /* Equipped gestures */
    *ptr += 4 * 6;
    return true;
}

static bool parse_projectiles_and_face(er_char_data_t *char_data, const uint8_t **ptr, const uint8_t *end) {
    /* Projectile */
    /* count */
    if (*ptr + 4 > end) return false;
    const uint32_t projectile_count = read_uint32(ptr);
    if (projectile_count > (uint32_t)(end - *ptr) / 8) return false;
    *ptr += 8 * projectile_count;

    /* equipped items */
    *ptr += 4 * 39;

    /* equip physics */
    *ptr += 4 * 2;

    *ptr += 4;

    *ptr += 4;

    char_data->face_offset = (uint32_t)(*ptr - char_data->data);
    /* face data */
    *ptr += ER_FACE_SECTION_SIZE;

    *ptr += 0x0B;
    return true;
}

static bool parse_inventory2_and_regions(er_char_data_t *char_data, const uint8_t **ptr, const uint8_t *end) {
    (void)char_data;
    /* inventory 2 */
    *ptr += 4;
    *ptr += 0x0C * ER_INV2_PART1_COUNT;
    *ptr += 4;
    *ptr += 0x0C * 0x80;
    *ptr += 4;
    *ptr += 4;

    /* gestures */
    *ptr += 4 * 0x40;

    /* regions */
    if (*ptr + 4 > end) return false;
    const uint32_t region_count = read_uint32(ptr);
    if (region_count > (uint32_t)(end - *ptr) / 4) return false;
    *ptr += 4 * region_count;

    /* rides */
    *ptr += 0x28;

    *ptr += 0x4D;
    return true;
}

static bool parse_trailing_data(er_char_data_t *char_data, const uint8_t **ptr, const uint8_t *end) {
    /* menu profile save load */
    *ptr += ER_MENU_PROFILE_SIZE;

    /* trophy equip data */
    *ptr += 0x34;

    /* item seen list */
    *ptr += 4;
    *ptr += 4;
    *ptr += ER_ITEM_SEEN_ENTRY_SIZE * ER_ITEM_SEEN_COUNT;

    /* tutorial data */
    *ptr += ER_TUTORIAL_DATA_SIZE;

    char_data->death_count_offset = (uint32_t)(*ptr - char_data->data);

    *ptr += 0x1A;

    /* flags */
    *ptr += ER_FLAGS_SIZE;

    *ptr += 1;

    for (int i = 0; i < 5; i++) {
        if (*ptr + 4 > end) return false;
        const uint32_t sz = read_uint32(ptr);
        if (sz > (uint32_t)(end - *ptr)) return false;
        *ptr += sz;
    }

    /* player coords */
    /* position (x, y, z, map_id) */
    *ptr += 0x10;
    *ptr += 0x11;
    /* position2 (x, y, z) */
    *ptr += 0xC;
    *ptr += 0x10;

    *ptr += 0xF;

    /* account active  2=active  0=empty */
    *ptr += 4;

    /* net data */
    *ptr += ER_NET_DATA_SIZE;

    /* weather info */
    *ptr += 4 * 6;

    *ptr += 0x10;

    char_data->userid_offset = (uint32_t)(*ptr - char_data->data);

    /* all rest data */
    return true;
}

static bool parse_char_slot(er_char_data_t *char_data) {
    char_data->userid_offset = 0;
    char_data->stats_offset = 0;
    char_data->face_offset = 0;
    const uint8_t *ptr = char_data->data + ER_CHAR_INITIAL_OFFSET;
    const uint8_t *end = char_data->data + sizeof(char_data->data);

    return parse_item_list(char_data, &ptr, end)
        && parse_character_info(char_data, &ptr, end)
        && parse_equipment(char_data, &ptr, end)
        && parse_inventory(char_data, &ptr, end)
        && parse_projectiles_and_face(char_data, &ptr, end)
        && parse_inventory2_and_regions(char_data, &ptr, end)
        && parse_trailing_data(char_data, &ptr, end);
}

/**
 * @brief Reads character slot data from the save file
 * @param char_data Pointer to character data structure to fill
 * @param file Handle to the open save file
 * @return true if read successful, false otherwise
 */
static bool read_char_slot(er_char_data_t *char_data, HANDLE file) {
    if (SetFilePointer(file, char_data->slot_offset + ER_SLOT_HEADER_SIZE, NULL, FILE_BEGIN) != char_data->slot_offset + ER_SLOT_HEADER_SIZE) {
        return false;
    }
    DWORD bytes_read;
    if (!ReadFile(file, char_data->data, sizeof(char_data->data), &bytes_read, NULL) || bytes_read != sizeof(char_data->data)) {
        return false;
    }
    return parse_char_slot(char_data);
}

/**
 * @brief Reads summary slot data from the save file
 * @param summary_data Pointer to summary data structure to fill
 * @param file Handle to the open save file
 * @return true if read successful, false otherwise
 */
static bool read_summary_slot(er_summary_data_t *summary_data, HANDLE file) {
    if (SetFilePointer(file, summary_data->slot_offset + ER_SLOT_HEADER_SIZE, NULL, FILE_BEGIN) != summary_data->slot_offset + ER_SLOT_HEADER_SIZE) {
        return false;
    }
    DWORD bytes_read;
    if (!ReadFile(file, summary_data->data, sizeof(summary_data->data), &bytes_read, NULL) || bytes_read != sizeof(summary_data->data)) {
        return false;
    }

    const uint8_t *ptr = summary_data->data + 4;
    /* userid */
    ptr += 8;
    ptr += ER_SUMMARY_DATA_LEAD_SIZE + 4;
    const uint32_t sz = read_uint32(&ptr);
    const uint8_t *ptr2 = ptr + 4;
    if (read_uint32(&ptr2) != ER_SUMMARY_FACE_SECTION_SIZE) {
        return false;
    }
    summary_data->face_offset = (uint32_t)(ptr2 - summary_data->data);
    ptr2 += ER_SUMMARY_FACE_SECTION_SIZE;
    ptr2 += 8;
    summary_data->active_offset = (uint32_t)(ptr2 - summary_data->data);
    ptr += sz;
    summary_data->available_offset = (uint32_t)(ptr - summary_data->data);
    ptr += 10;
    summary_data->profile_offset = (uint32_t)(ptr - summary_data->data);
    return true;
}

er_save_data_t *er_save_data_load(const wchar_t *path) {
    if (!path_fits_fixed_buffer(path)) {
        return NULL;
    }

    er_save_data_t *save_data = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(er_save_data_t));
    if (!save_data) {
        return NULL;
    }

    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        LocalFree(save_data);
        return NULL;
    }

    DWORD bytes_read;
    uint8_t sig[4];
    if (!ReadFile(file, sig, 4, &bytes_read, NULL) || bytes_read != 4 || RtlCompareMemory(sig, "BND4", 4) < 4) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }
    uint8_t header[ER_FILE_HEADER_SIZE];
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    if (!ReadFile(file, header, sizeof(header), &bytes_read, NULL) || bytes_read != sizeof(header)) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }
    int save_slot_count = *(int *)&header[ER_HEADER_SLOT_COUNT_OFFSET];
    if (save_slot_count < 12) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }

    uint32_t save_slot_size = *(uint32_t *)(&header[ER_HEADER_SLOT_SIZE_BASE + 10 * ER_HEADER_SLOT_STRIDE]);
    if (save_slot_size != ER_SUMMARY_SLOT_FILE_SIZE) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }
    save_data->summary_data.slot_offset = *(uint32_t *)(&header[ER_HEADER_SLOT_OFFSET_BASE + 10 * ER_HEADER_SLOT_STRIDE]);
    if (!read_summary_slot(&save_data->summary_data, file)) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }

    for (int i = 0; i < 10; i++) {
        ZeroMemory(&save_data->char_data[i], sizeof(er_char_data_t));
        save_slot_size = *(uint32_t *)(&header[ER_HEADER_SLOT_SIZE_BASE + i * ER_HEADER_SLOT_STRIDE]);
        if (save_slot_size != ER_CHAR_SLOT_FILE_SIZE) {
            LocalFree(save_data);
            CloseHandle(file);
            return NULL;
        }
        save_data->char_data[i].slot_offset = *(uint32_t *)(&header[ER_HEADER_SLOT_OFFSET_BASE + i * ER_HEADER_SLOT_STRIDE]);
        if (save_data->summary_data.data[save_data->summary_data.available_offset + i]) {
            if (!read_char_slot(&save_data->char_data[i], file)) {
                LocalFree(save_data);
                CloseHandle(file);
                return NULL;
            }
            CopyMemory(save_data->char_data[i].profile, save_data->summary_data.data + save_data->summary_data.profile_offset + ER_PROFILE_SIZE * i, ER_PROFILE_SIZE);
        }
    }

    save_data->regulation_slot_offset = *(uint32_t *)(&header[ER_HEADER_SLOT_OFFSET_BASE + 11 * ER_HEADER_SLOT_STRIDE]);
    save_data->regulation_slot_size = *(uint32_t *)(&header[ER_HEADER_SLOT_SIZE_BASE + 11 * ER_HEADER_SLOT_STRIDE]);
    read_regulation_header(save_data, file, save_data->regulation_slot_offset,
                           save_data->regulation_slot_size);

    CloseHandle(file);
    lstrcpyW(save_data->full_path, path);
    return save_data;
}

void er_save_data_free(er_save_data_t *save_data) {
    if (save_data) {
        LocalFree(save_data);
    }
}

er_save_simple_data_t *er_save_simple_data_load(const wchar_t *path) {
    if (!path_fits_fixed_buffer(path)) {
        return NULL;
    }

    er_save_simple_data_t *save_data = LocalAlloc(LMEM_FIXED, sizeof(er_save_simple_data_t));
    if (!save_data) {
        return NULL;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        LocalFree(save_data);
        return NULL;
    }

    DWORD bytes_read;
    uint8_t sig[4];
    if (!ReadFile(file, sig, 4, &bytes_read, NULL) || bytes_read != 4 || RtlCompareMemory(sig, "BND4", 4) < 4) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }
    uint8_t header[ER_FILE_HEADER_SIZE];
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    if (!ReadFile(file, header, sizeof(header), &bytes_read, NULL) || bytes_read != sizeof(header)) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }
    int save_slot_count = *(int *)&header[ER_HEADER_SLOT_COUNT_OFFSET];
    if (save_slot_count < 12) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }

    for (int i = 0; i < 10; i++) {
        save_data->slot_offset[i] = *(uint32_t *)(&header[ER_HEADER_SLOT_OFFSET_BASE + i * ER_HEADER_SLOT_STRIDE]);
    }

    uint32_t save_slot_size = *(uint32_t *)(&header[ER_HEADER_SLOT_SIZE_BASE + 10 * ER_HEADER_SLOT_STRIDE]);
    if (save_slot_size != ER_SUMMARY_SLOT_FILE_SIZE) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }
    er_summary_data_t *summary_data = LocalAlloc(LMEM_FIXED, sizeof(er_summary_data_t));
    if (!summary_data) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }
    summary_data->slot_offset = save_data->summary_slot_offset = *(uint32_t *)(&header[ER_HEADER_SLOT_OFFSET_BASE + 10 * ER_HEADER_SLOT_STRIDE]);
    if (!read_summary_slot(summary_data, file)) {
        LocalFree(summary_data);
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }
    save_data->summary_profile_offset = summary_data->profile_offset;
    const uint8_t *available_ptr = summary_data->data + summary_data->available_offset;
    for (int i = 0; i < 10; i++) {
        if (available_ptr[i]) {
            if (!lstrcpynW(save_data->char_name[i],
                           (wchar_t *)(summary_data->data + summary_data->profile_offset + ER_PROFILE_SIZE * i),
                           32)) {
                LocalFree(summary_data);
                LocalFree(save_data);
                CloseHandle(file);
                return NULL;
            }
        } else {
            save_data->char_name[i][0] = 0;
        }
    }
    LocalFree(summary_data);

    CloseHandle(file);
    lstrcpyW(save_data->full_path, path);
    return save_data;
}

void er_save_simple_data_free(er_save_simple_data_t *save_data) {
    if (save_data) {
        LocalFree(save_data);
    }
}

const wchar_t *er_save_simple_data_get_char_name(const er_save_simple_data_t *save_data, int slot) {
    if (!save_data || slot < 0 || slot >= 10) {
        return NULL;
    }
    return save_data->char_name[slot];
}

uint8_t *er_save_simple_data_slot_export(const er_save_simple_data_t *save_data, int slot) {
    if (!save_data || slot < 0 || slot >= 10) {
        return NULL;
    }
    HANDLE file = CreateFileW(save_data->full_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    uint8_t *slot_data = LocalAlloc(LMEM_FIXED, ER_CHAR_DATA_SIZE + ER_PROFILE_SIZE);
    if (!slot_data) {
        CloseHandle(file);
        return NULL;
    }
    if (SetFilePointer(file, save_data->slot_offset[slot] + ER_SLOT_HEADER_SIZE, NULL, FILE_BEGIN) != save_data->slot_offset[slot] + ER_SLOT_HEADER_SIZE) {
        LocalFree(slot_data);
        CloseHandle(file);
        return NULL;
    }
    DWORD bytes_read;
    if (!ReadFile(file, slot_data, ER_CHAR_DATA_SIZE, &bytes_read, NULL) || bytes_read != ER_CHAR_DATA_SIZE) {
        LocalFree(slot_data);
        CloseHandle(file);
        return NULL;
    }
    if (SetFilePointer(file, save_data->summary_slot_offset + ER_SLOT_HEADER_SIZE + save_data->summary_profile_offset + ER_PROFILE_SIZE * slot, NULL, FILE_BEGIN) != save_data->summary_slot_offset + ER_SLOT_HEADER_SIZE + save_data->summary_profile_offset + ER_PROFILE_SIZE * slot) {
        LocalFree(slot_data);
        CloseHandle(file);
        return NULL;
    }
    if (!ReadFile(file, slot_data + ER_CHAR_DATA_SIZE, ER_PROFILE_SIZE, &bytes_read, NULL) || bytes_read != ER_PROFILE_SIZE) {
        LocalFree(slot_data);
        CloseHandle(file);
        return NULL;
    }
    CloseHandle(file);
    return slot_data;
}

void er_save_simple_data_slot_free(uint8_t *slot_data) {
    if (slot_data) {
        LocalFree(slot_data);
    }
}

uint64_t er_save_get_userid(const er_save_data_t *save_data) {
    if (!save_data) {
        return 0ULL;
    }
    return *(uint64_t *)(save_data->summary_data.data + 0x04);
}

bool er_save_get_active_slot(const er_save_data_t *save_data, int *out_slot) {
    if (!save_data || !out_slot) {
        return false;
    }
    if (save_data->summary_data.active_offset >= ER_SUMMARY_DATA_SIZE) {
        return false;
    }
    int slot = (int)save_data->summary_data.data[save_data->summary_data.active_offset];
    if (slot < 0 || slot > 9) {
        return false;
    }
    *out_slot = slot;
    return true;
}

size_t er_save_version_target_count(void) {
    return sizeof(version_targets) / sizeof(version_targets[0]);
}

const er_version_target_t *er_save_version_target_get(size_t index) {
    if (index >= er_save_version_target_count()) {
        return NULL;
    }
    return &version_targets[index];
}

size_t er_save_downgrade_target_count(void) {
    return sizeof(save_downgrade_targets) / sizeof(save_downgrade_targets[0]);
}

const er_save_downgrade_target_t *er_save_downgrade_target_get(size_t index) {
    if (index >= er_save_downgrade_target_count()) {
        return NULL;
    }
    return &save_downgrade_targets[index];
}

bool er_char_data_version_info(const er_char_data_t *char_data, er_char_version_info_t *info) {
    uint32_t trailing_offset;

    if (!char_data || !info || char_data->userid_offset < ER_CHAR_TRAILING_VERSION_SIZE
        || char_data->userid_offset > ER_CHAR_DATA_SIZE) {
        return false;
    }

    trailing_offset = char_data->userid_offset - ER_CHAR_TRAILING_VERSION_SIZE;
    info->version = *(const uint32_t *)(char_data->data + ER_CHAR_VERSION_OFFSET);
    info->trailing_versions[0] = *(const uint32_t *)(char_data->data + trailing_offset);
    info->trailing_versions[1] = *(const uint32_t *)(char_data->data + trailing_offset + sizeof(uint32_t));
    return true;
}

bool er_save_version_info(const er_save_data_t *save_data, er_save_version_info_t *info) {
    if (!save_data || !info) {
        return false;
    }

    ZeroMemory(info, sizeof(*info));
    info->summary_version = *(const uint32_t *)save_data->summary_data.data;
    for (int i = 0; i < 10; i++) {
        er_char_version_info_t char_info;

        if (save_data->summary_data.data[save_data->summary_data.available_offset + i]
            && er_char_data_version_info(&save_data->char_data[i], &char_info)) {
            info->character_versions[i] = char_info.version;
        }
    }

    if (save_data->regulation_header[0] == ER_REGULATION_MAGIC) {
        info->regulation_header_valid = true;
        info->regulation_header_version = save_data->regulation_header[1];
        info->regulation_build = save_data->regulation_header[2];
    }
    return true;
}

bool er_save_downgrade_character(er_save_data_t *save_data, int slot,
                                 uint32_t target_character_version) {
    er_char_version_info_t version_info;
    er_char_data_t *char_data;
    uint32_t trailing_offset;
    uint32_t original_versions[3];
    uint8_t updated_md5[ER_SLOT_HEADER_SIZE];
    wchar_t temp_path[MAX_PATH];
    HANDLE file;
    HANDLE source_file;
    uint8_t *source_snapshot;
    DWORD source_size;
    bool ok;

    if (!save_data || slot < 0 || slot >= 10
        || !is_known_character_version(target_character_version)
        || !save_data->summary_data.data[save_data->summary_data.available_offset + slot]) {
        return false;
    }

    char_data = &save_data->char_data[slot];
    if (!er_char_data_version_info(char_data, &version_info)
        || target_character_version >= version_info.version) {
        return false;
    }

    trailing_offset = char_data->userid_offset - ER_CHAR_TRAILING_VERSION_SIZE;
    original_versions[0] = version_info.version;
    original_versions[1] = version_info.trailing_versions[0];
    original_versions[2] = version_info.trailing_versions[1];
    file = make_verified_downgrade_temp_copy(save_data, temp_path, &source_file,
                                             &source_snapshot, &source_size);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    *(uint32_t *)(char_data->data + ER_CHAR_VERSION_OFFSET) = target_character_version;
    for (int i = 0; i < 2; i++) {
        uint32_t *field = (uint32_t *)(char_data->data + trailing_offset + sizeof(uint32_t) * i);
        if (*field > target_character_version) {
            *field = target_character_version;
        }
    }
    md5_buffer(char_data->data, sizeof(char_data->data), updated_md5);

    ok = write_at(file, char_data->slot_offset + ER_SLOT_HEADER_SIZE + ER_CHAR_VERSION_OFFSET,
                  char_data->data + ER_CHAR_VERSION_OFFSET, sizeof(uint32_t));
    ok = ok && write_at(file, char_data->slot_offset + ER_SLOT_HEADER_SIZE + trailing_offset,
                        char_data->data + trailing_offset, sizeof(uint32_t) * 2);
    ok = ok && write_at(file, char_data->slot_offset, updated_md5, sizeof(updated_md5));
    if (ok) {
        ok = commit_downgrade_temp_copy(save_data->full_path, temp_path, file,
                                        source_file, source_snapshot, source_size);
        file = INVALID_HANDLE_VALUE;
        source_file = INVALID_HANDLE_VALUE;
    }
    if (!ok) {
        *(uint32_t *)(char_data->data + ER_CHAR_VERSION_OFFSET) = original_versions[0];
        *(uint32_t *)(char_data->data + trailing_offset) = original_versions[1];
        *(uint32_t *)(char_data->data + trailing_offset + sizeof(uint32_t)) = original_versions[2];
    }
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        CloseHandle(source_file);
        DeleteFileW(temp_path);
    }
    LocalFree(source_snapshot);
    return ok;
}

bool er_save_downgrade(er_save_data_t *save_data,
                       const er_save_downgrade_target_t *target,
                       const wchar_t *regulation_path) {
    uint8_t *regulation_data;
    uint8_t *regulation_payload;
    uint8_t char_md5[10][ER_SLOT_HEADER_SIZE];
    uint8_t summary_md5[ER_SLOT_HEADER_SIZE];
    uint8_t regulation_md5[ER_SLOT_HEADER_SIZE];
    uint8_t regulation_file_md5[ER_SLOT_HEADER_SIZE];
    uint32_t original_char_versions[10][3] = {0};
    uint32_t original_summary_version;
    uint32_t original_regulation_header[4];
    uint64_t expected_regulation_offset;
    DWORD regulation_size;
    wchar_t temp_path[MAX_PATH];
    HANDLE file;
    HANDLE source_file;
    uint8_t *source_snapshot;
    DWORD source_size;
    bool ok = true;

    if (!save_data || !downgrade_target_is_known(target) || !regulation_path) {
        return false;
    }
    expected_regulation_offset = (uint64_t)save_data->summary_data.slot_offset
        + ER_SUMMARY_SLOT_FILE_SIZE;
    if (expected_regulation_offset > UINT32_MAX
        || save_data->regulation_slot_offset != (uint32_t)expected_regulation_offset
        || save_data->regulation_slot_size != ER_REGULATION_SLOT_FILE_SIZE
        || target->summary_version >= *(const uint32_t *)save_data->summary_data.data) {
        return false;
    }

    regulation_data = read_regulation_file(regulation_path, &regulation_size);
    if (!regulation_data || regulation_size != target->regulation_size) {
        if (regulation_data) {
            LocalFree(regulation_data);
        }
        return false;
    }
    md5_buffer(regulation_data, regulation_size, regulation_file_md5);
    if (RtlCompareMemory(regulation_file_md5, target->regulation_md5,
                         sizeof(regulation_file_md5)) != sizeof(regulation_file_md5)) {
        LocalFree(regulation_data);
        return false;
    }

    regulation_payload = (uint8_t *)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT,
                                               ER_REGULATION_DATA_SIZE);
    if (!regulation_payload) {
        LocalFree(regulation_data);
        return false;
    }
    CopyMemory(regulation_payload, regulation_data, regulation_size);
    LocalFree(regulation_data);
    for (int i = 0; i < 16; i++) {
        if (regulation_payload[i] != 0) {
            LocalFree(regulation_payload);
            return false;
        }
    }

    original_summary_version = *(uint32_t *)save_data->summary_data.data;
    CopyMemory(original_regulation_header, save_data->regulation_header,
               sizeof(original_regulation_header));
    for (int i = 0; i < 10; i++) {
        er_char_version_info_t info;

        if (!save_data->summary_data.data[save_data->summary_data.available_offset + i]) {
            continue;
        }
        if (!er_char_data_version_info(&save_data->char_data[i], &info)) {
            LocalFree(regulation_payload);
            return false;
        }
        original_char_versions[i][0] = info.version;
        original_char_versions[i][1] = info.trailing_versions[0];
        original_char_versions[i][2] = info.trailing_versions[1];
    }

    file = make_verified_downgrade_temp_copy(save_data, temp_path, &source_file,
                                             &source_snapshot, &source_size);
    if (file == INVALID_HANDLE_VALUE) {
        LocalFree(regulation_payload);
        return false;
    }

    *(uint32_t *)save_data->summary_data.data = target->summary_version;
    save_data->regulation_header[0] = ER_REGULATION_MAGIC;
    save_data->regulation_header[1] = 2;
    save_data->regulation_header[2] = target->regulation_build;
    save_data->regulation_header[3] = ER_REGULATION_DATA_SIZE;
    for (int i = 0; i < 10; i++) {
        er_char_data_t *char_data;
        uint32_t trailing_offset;
        uint32_t *version;

        if (!save_data->summary_data.data[save_data->summary_data.available_offset + i]) {
            continue;
        }
        char_data = &save_data->char_data[i];
        trailing_offset = char_data->userid_offset - ER_CHAR_TRAILING_VERSION_SIZE;
        version = (uint32_t *)(char_data->data + ER_CHAR_VERSION_OFFSET);

        if (*version > target->character_version) {
            *version = target->character_version;
        }
        for (int j = 0; j < 2; j++) {
            uint32_t *field = (uint32_t *)(char_data->data + trailing_offset + sizeof(uint32_t) * j);
            if (*field > target->character_version) {
                *field = target->character_version;
            }
        }
        md5_buffer(char_data->data, sizeof(char_data->data), char_md5[i]);
    }
    md5_buffer(save_data->summary_data.data, sizeof(save_data->summary_data.data), summary_md5);
    {
        uint8_t *slot_payload = (uint8_t *)LocalAlloc(LMEM_FIXED, ER_REGULATION_HEADER_SIZE + ER_REGULATION_DATA_SIZE);
        if (!slot_payload) {
            ok = false;
        } else {
            CopyMemory(slot_payload, save_data->regulation_header, ER_REGULATION_HEADER_SIZE);
            CopyMemory(slot_payload + ER_REGULATION_HEADER_SIZE, regulation_payload,
                       ER_REGULATION_DATA_SIZE);
            md5_buffer(slot_payload, ER_REGULATION_HEADER_SIZE + ER_REGULATION_DATA_SIZE,
                       regulation_md5);
            LocalFree(slot_payload);
        }
    }

    if (ok) {
        for (int i = 0; i < 10 && ok; i++) {
            er_char_data_t *char_data;

            if (!save_data->summary_data.data[save_data->summary_data.available_offset + i]) {
                continue;
            }
            char_data = &save_data->char_data[i];
            ok = write_at(file, char_data->slot_offset, char_md5[i], ER_SLOT_HEADER_SIZE)
                && write_at(file, char_data->slot_offset + ER_SLOT_HEADER_SIZE,
                            char_data->data, sizeof(char_data->data));
        }
        ok = ok && write_at(file, save_data->summary_data.slot_offset, summary_md5,
                            ER_SLOT_HEADER_SIZE)
            && write_at(file, save_data->summary_data.slot_offset + ER_SLOT_HEADER_SIZE,
                        save_data->summary_data.data, sizeof(save_data->summary_data.data))
            && write_at(file, save_data->regulation_slot_offset, regulation_md5,
                        ER_SLOT_HEADER_SIZE)
            && write_at(file, save_data->regulation_slot_offset + ER_SLOT_HEADER_SIZE,
                        save_data->regulation_header, ER_REGULATION_HEADER_SIZE)
            && write_at(file, save_data->regulation_slot_offset + ER_SLOT_HEADER_SIZE
                              + ER_REGULATION_HEADER_SIZE,
                        regulation_payload, ER_REGULATION_DATA_SIZE);
        if (ok) {
            ok = commit_downgrade_temp_copy(save_data->full_path, temp_path, file,
                                            source_file, source_snapshot, source_size);
            file = INVALID_HANDLE_VALUE;
            source_file = INVALID_HANDLE_VALUE;
        }
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
            CloseHandle(source_file);
            DeleteFileW(temp_path);
            file = INVALID_HANDLE_VALUE;
        }
    } else {
        CloseHandle(file);
        CloseHandle(source_file);
        DeleteFileW(temp_path);
        file = INVALID_HANDLE_VALUE;
    }

    LocalFree(source_snapshot);
    if (!ok) {
        *(uint32_t *)save_data->summary_data.data = original_summary_version;
        CopyMemory(save_data->regulation_header, original_regulation_header,
                   sizeof(original_regulation_header));
        for (int i = 0; i < 10; i++) {
            er_char_data_t *char_data;
            uint32_t trailing_offset;

            if (!save_data->summary_data.data[save_data->summary_data.available_offset + i]) {
                continue;
            }
            char_data = &save_data->char_data[i];
            trailing_offset = char_data->userid_offset - ER_CHAR_TRAILING_VERSION_SIZE;
            *(uint32_t *)(char_data->data + ER_CHAR_VERSION_OFFSET) = original_char_versions[i][0];
            *(uint32_t *)(char_data->data + trailing_offset) = original_char_versions[i][1];
            *(uint32_t *)(char_data->data + trailing_offset + sizeof(uint32_t)) = original_char_versions[i][2];
        }
    }

    LocalFree(regulation_payload);
    return ok;
}

bool er_save_resign_userid(er_save_data_t *save_data, uint64_t user_id) {
    if (!save_data) {
        return false;
    }
    HANDLE file = CreateFileW(save_data->full_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    uint8_t md5[0x10];
    const er_summary_data_t *summary_data = &save_data->summary_data;
    bool ok = true;
    for (int i = 0; i < 10; i++) {
        if (!summary_data->data[summary_data->available_offset + i]) continue;
        er_char_data_t *char_data = &save_data->char_data[i];
        *(uint64_t *)(char_data->data + char_data->userid_offset) = user_id;
        md5_buffer(char_data->data, sizeof(char_data->data), md5);
        ok = ok && write_at(file, char_data->slot_offset, md5, sizeof(md5));
        ok = ok && write_at(file, char_data->slot_offset + ER_SLOT_HEADER_SIZE + char_data->userid_offset, &user_id, sizeof(user_id));
    }
    *(uint64_t *)(save_data->summary_data.data + 4) = user_id;
    md5_buffer(save_data->summary_data.data, sizeof(save_data->summary_data.data), md5);
    ok = ok && write_at(file, save_data->summary_data.slot_offset, md5, sizeof(md5));
    ok = ok && write_at(file, save_data->summary_data.slot_offset + ER_SLOT_HEADER_SIZE + 0x04, &user_id, sizeof(user_id));
    CloseHandle(file);
    return ok;
}

int er_save_debug_get_active_slot_byte(const er_save_data_t *save) {
    if (!save) {
        return -1;
    }
    if (save->summary_data.active_offset >= ER_SUMMARY_DATA_SIZE) {
        return -1;
    }
    return (int)save->summary_data.data[save->summary_data.active_offset];
}

uint32_t er_save_debug_get_active_offset(const er_save_data_t *save) {
    if (!save) {
        return 0;
    }
    return save->summary_data.active_offset;
}

bool er_save_debug_set_active_slot_byte(er_save_data_t *save, uint8_t value, const wchar_t *persist_path) {
    if (!save || !persist_path) {
        return false;
    }
    if (save->summary_data.active_offset >= ER_SUMMARY_DATA_SIZE) {
        return false;
    }

    save->summary_data.data[save->summary_data.active_offset] = value;

    HANDLE file = CreateFileW(persist_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    uint8_t md5[0x10];
    md5_buffer(save->summary_data.data, sizeof(save->summary_data.data), md5);

    bool ok = write_at(file, save->summary_data.slot_offset, md5, sizeof(md5));
    ok = ok && write_at(file, save->summary_data.slot_offset + ER_SLOT_HEADER_SIZE + save->summary_data.active_offset, &value, 1);

    CloseHandle(file);
    return ok;
}

const er_char_data_t *er_char_data_ref(const er_save_data_t *save_data, int slot) {
    if (!save_data) {
        return NULL;
    }
    if (slot < 0 || slot >= 10) {
        return NULL;
    }
    const er_summary_data_t *summary_data = &save_data->summary_data;
    if (!summary_data->data[summary_data->available_offset + slot]) {
        return NULL;
    }
    return &save_data->char_data[slot];
}

bool er_char_data_import(er_save_data_t *save_data, int slot, const er_char_data_t *char_data) {
    if (!save_data || slot < 0 || slot >= 10 || !char_data) {
        return false;
    }
    HANDLE file = CreateFileW(save_data->full_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    er_char_data_t *data = &save_data->char_data[slot];
    er_summary_data_t *summary_data = &save_data->summary_data;
    uint32_t slot_offset = data->slot_offset;
    CopyMemory(data, char_data, sizeof(er_char_data_t));
    data->slot_offset = slot_offset;
    CopyMemory(summary_data->data + summary_data->profile_offset + ER_PROFILE_SIZE * slot, char_data->profile, ER_PROFILE_SIZE);
    *(summary_data->data + summary_data->available_offset + slot) = 1;

    /* Update userid */
    *(uint64_t *)(data->data + data->userid_offset) = *(uint64_t *)(summary_data->data + 0x04);

    uint8_t md5[0x10];
    md5_buffer(data->data, sizeof(data->data), md5);
    bool ok = write_at(file, data->slot_offset, md5, sizeof(md5));
    ok = ok && write_at(file, data->slot_offset + ER_SLOT_HEADER_SIZE, data->data, sizeof(data->data));

    md5_buffer(summary_data->data, sizeof(summary_data->data), md5);
    ok = ok && write_at(file, summary_data->slot_offset, md5, sizeof(md5));
    ok = ok && write_at(file, summary_data->slot_offset + ER_SLOT_HEADER_SIZE + summary_data->profile_offset + ER_PROFILE_SIZE * slot, char_data->profile, ER_PROFILE_SIZE);
    const uint8_t byte_avail = 1;
    ok = ok && write_at(file, summary_data->slot_offset + ER_SLOT_HEADER_SIZE + summary_data->available_offset + slot, &byte_avail, sizeof(byte_avail));

    CloseHandle(file);
    return ok;
}

bool er_char_data_import_raw(er_save_data_t *save_data, int slot, const uint8_t *raw_data) {
    if (!save_data || slot < 0 || slot >= 10 || !raw_data) {
        return false;
    }
    er_char_data_t *new_char = er_char_data_from_memory(raw_data);
    bool result = er_char_data_import(save_data, slot, new_char);
    er_char_data_free(new_char);
    return result;
}

bool er_char_data_set_name(er_save_data_t *save_data, int slot, const wchar_t *name) {
    if (!save_data || slot < 0 || slot >= 10 || !name) {
        return false;
    }

    HANDLE file = CreateFileW(save_data->full_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    er_summary_data_t *summary_data = &save_data->summary_data;
    if (!summary_data->data[summary_data->available_offset + slot]) {
        CloseHandle(file);
        return false;
    }
    er_char_data_t *char_data = &save_data->char_data[slot];
    if (!copy_name_field(char_data->profile, ER_CHAR_NAME_SIZE, name)
        || !copy_name_field(char_data->data + char_data->stats_offset + 4 * 37, ER_CHAR_NAME_SIZE, name)
        || !copy_name_field(summary_data->data + summary_data->profile_offset + ER_PROFILE_SIZE * slot,
                            ER_CHAR_NAME_SIZE, name)) {
        CloseHandle(file);
        return false;
    }

    uint8_t md5[0x10];
    md5_buffer(char_data->data, sizeof(char_data->data), md5);
    bool ok = write_at(file, char_data->slot_offset, md5, sizeof(md5));
    ok = ok && write_at(file, char_data->slot_offset + ER_SLOT_HEADER_SIZE + char_data->stats_offset + 4 * 37,
                        char_data->data + char_data->stats_offset + 4 * 37, ER_CHAR_NAME_SIZE);

    md5_buffer(summary_data->data, sizeof(summary_data->data), md5);
    ok = ok && write_at(file, summary_data->slot_offset, md5, sizeof(md5));
    ok = ok && write_at(file, summary_data->slot_offset + ER_SLOT_HEADER_SIZE + summary_data->profile_offset + ER_PROFILE_SIZE * slot,
                        summary_data->data + summary_data->profile_offset + ER_PROFILE_SIZE * slot, ER_CHAR_NAME_SIZE);

    CloseHandle(file);
    return ok;
}


const wchar_t *er_char_data_get_name(const er_char_data_t *char_data) {
    if (!char_data) {
        return NULL;
    }
    return (const wchar_t *)(char_data->data + char_data->stats_offset + 4 * 37);
}

bool er_char_data_info(const er_char_data_t *char_data, er_char_info_t *info) {
    if (!char_data || !info) {
        return false;
    }
    info->in_game_time = *(uint32_t *)(char_data->data + 8);
    info->body_type = *(char_data->data + char_data->stats_offset + ER_STATS_SECTION_SIZE + ER_CHAR_NAME_SIZE);
    info->level = *(uint32_t *)(char_data->data + char_data->stats_offset + 4 * 24);
    const uint32_t *stats_ptr = (const uint32_t *)(char_data->data + char_data->stats_offset + 4 * 13);
    CopyMemory(info->stats, stats_ptr, sizeof(info->stats));
    info->runes_held = *(uint32_t *)(char_data->data + char_data->stats_offset + 4 * 25);
    info->death_count = *(uint32_t *)(char_data->data + char_data->death_count_offset);
    return true;
}

er_char_data_t *er_char_data_from_file(const wchar_t *path) {
    if (!path) {
        return NULL;
    }

    er_char_data_t *char_data = LocalAlloc(LMEM_FIXED, sizeof(er_char_data_t));
    if (!char_data) {
        return NULL;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        LocalFree(char_data);
        return NULL;
    }
    DWORD bytes_read;
    if (!ReadFile(file, char_data->data, sizeof(char_data->data), &bytes_read, NULL) || bytes_read != sizeof(char_data->data)) {
        LocalFree(char_data);
        CloseHandle(file);
        return NULL;
    }
    if (!parse_char_slot(char_data)) {
        LocalFree(char_data);
        CloseHandle(file);
        return NULL;
    }
    if (!ReadFile(file, char_data->profile, ER_PROFILE_SIZE, &bytes_read, NULL) || bytes_read != ER_PROFILE_SIZE) {
        LocalFree(char_data);
        CloseHandle(file);
        return NULL;
    }
    CloseHandle(file);

    return char_data;
}

er_char_data_t *er_char_data_from_memory(const uint8_t *data) {
    if (!data) {
        return NULL;
    }

    er_char_data_t *char_data = LocalAlloc(LMEM_FIXED, sizeof(er_char_data_t));
    if (!char_data) {
        return NULL;
    }
    CopyMemory(char_data->data, data, ER_CHAR_DATA_SIZE);
    if (!parse_char_slot(char_data)) {
        LocalFree(char_data);
        return NULL;
    }
    CopyMemory(char_data->profile, data + ER_CHAR_DATA_SIZE, ER_PROFILE_SIZE);
    return char_data;
}

bool er_char_data_to_file(const er_char_data_t *char_data, const wchar_t *path) {
    if (!char_data || !path) {
        return false;
    }

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written;
    bool ok = WriteFile(file, char_data->data, sizeof(char_data->data), &written, NULL) && written == sizeof(char_data->data);
    ok = ok && WriteFile(file, char_data->profile, ER_PROFILE_SIZE, &written, NULL) && written == ER_PROFILE_SIZE;
    CloseHandle(file);
    return ok;
}

bool er_char_data_serialize(const er_char_data_t *c, uint8_t *out, size_t out_size) {
    if (!c || !out || out_size < ER_CHAR_DATA_SIZE + ER_PROFILE_SIZE) {
        return false;
    }
    CopyMemory(out, c->data, ER_CHAR_DATA_SIZE);
    CopyMemory(out + ER_CHAR_DATA_SIZE, c->profile, ER_PROFILE_SIZE);
    return true;
}

void er_char_data_free(er_char_data_t *char_data) {
    if (char_data) {
        LocalFree(char_data);
    }
}

const uint8_t *er_face_data_ref(const er_save_data_t *save_data, int slot) {
    if (!save_data) {
        return NULL;
    }
    if (slot < 0 || slot >= 15) {
        return NULL;
    }
    const er_summary_data_t *summary_data = &save_data->summary_data;
    return summary_data->data + summary_data->face_offset + ER_FACE_DATA_SIZE * slot;
}

bool er_face_data_import(er_save_data_t *save_data, int slot, const uint8_t *face_data) {
    if (!save_data || slot < 0 || slot >= 15 || !validate_face_data(face_data)) {
        return false;
    }
    er_summary_data_t *summary_data = &save_data->summary_data;
    CopyMemory(summary_data->data + summary_data->face_offset + ER_FACE_DATA_SIZE * slot, face_data, ER_FACE_DATA_SIZE);
    uint8_t md5[0x10];
    md5_buffer(summary_data->data, sizeof(summary_data->data), md5);

    HANDLE file = CreateFileW(save_data->full_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = write_at(file, summary_data->slot_offset, md5, sizeof(md5));
    ok = ok && write_at(file, summary_data->slot_offset + ER_SLOT_HEADER_SIZE + summary_data->face_offset + ER_FACE_DATA_SIZE * slot,
                        summary_data->data + summary_data->face_offset + ER_FACE_DATA_SIZE * slot, ER_FACE_DATA_SIZE);
    CloseHandle(file);
    return ok;
}

void er_face_data_info(const uint8_t *face_data, uint8_t *available, uint8_t *gender) {
    if (!face_data) {
        return;
    }
    if (available) {
        *available = face_data[0x0];
    }
    if (gender) {
        *gender = face_data[0x1];
    }
}

uint8_t *er_face_data_from_file(const wchar_t *path) {
    if (!path) {
        return NULL;
    }

    uint8_t *face_data = LocalAlloc(LMEM_FIXED, ER_FACE_DATA_SIZE);
    if (!face_data) {
        return NULL;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        LocalFree(face_data);
        return NULL;
    }
    DWORD bytes_read;
    if (!ReadFile(file, face_data, ER_FACE_DATA_SIZE, &bytes_read, NULL) || bytes_read != ER_FACE_DATA_SIZE) {
        LocalFree(face_data);
        CloseHandle(file);
        return NULL;
    }
    CloseHandle(file);
    if (!validate_face_data(face_data)) {
        LocalFree(face_data);
        return NULL;
    }
    return face_data;
}

bool er_face_data_to_file(const uint8_t *face_data, const wchar_t *path) {
    if (!face_data || !path) {
        return false;
    }

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written;
    bool ok = WriteFile(file, face_data, ER_FACE_DATA_SIZE, &written, NULL) && written == ER_FACE_DATA_SIZE;
    CloseHandle(file);
    return ok;
}

void er_face_data_free(uint8_t *face_data) {
    if (face_data) {
        LocalFree(face_data);
    }
}
