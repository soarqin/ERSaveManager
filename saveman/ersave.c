/**
 * @file ersave.c
 * @brief Implementation of Elden Ring face data management functions
 * @details This file contains the implementation of functions for handling face data operations
 *          in Elden Ring save files, including loading, saving, importing and exporting face data.
 */

#include "ersave.h"

#include <md5.h>

#include <windows.h>

/* Structure to hold summary data - Contains face data for all character slots */
typedef struct er_summary_data_s {
    uint8_t data[0x60000]; /* Raw summary data buffer - Stores face data for all slots */
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
    uint8_t data[0x280000]; /* Raw character data buffer - Stores complete character data */
    uint8_t profile[0x24C]; /* Raw profile data buffer - Stores profile data */
} er_char_data_t;

/* Structure to hold complete save data - Contains all character slots and summary data */
typedef struct er_save_data_s {
    wchar_t full_path[MAX_PATH]; /* Full path to save file - Stores the complete file path */
    er_char_data_t char_data[10]; /* Array of character data slots - Stores data for up to 10 characters */
    er_summary_data_t summary_data; /* Summary data structure - Contains face data for all slots */
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
static bool validate_face_data(const uint8_t *face_data) { return face_data[0x00] == 0x01 && RtlCompareMemory(face_data + 0x10, "FACE", 4) == 4; }

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

static bool parse_char_slot(er_char_data_t *char_data) {
    char_data->userid_offset = 0;
    char_data->stats_offset = 0;
    char_data->face_offset = 0;
    const uint8_t *ptr = char_data->data + 0x20;
    const uint8_t *end = char_data->data + sizeof(char_data->data);
    /* item list */
    for (size_t i = 0; i < 0x1400; i++) {
        ptr += 4;
        const uint32_t itemId = read_uint32(&ptr);
        if (itemId == 0 || itemId == 0xFFFFFFFFu) continue;
        switch (itemId >> 28) {
            case 0:
                ptr += 13;
                break;
            case 1:
                ptr += 8;
                break;
            default:
                break;
        }
    }
    if (ptr > end) return false;
    char_data->stats_offset = (uint32_t)(ptr - char_data->data);
    /* character data */
    ptr += 0x94;
    /* charname */
    ptr += 0x22;
    /* gender */
    ptr += 1;
    /* birth job */
    ptr += 1;
    ptr += 3;
    /* gift */
    ptr += 1;
    ptr += 0x1E;
    /* match making weapn level */
    ptr += 1;
    ptr += 0x35;
    /* passwords */
    ptr += 0x12 * 6;
    ptr += 0x34;

    ptr += 0xd0;

    /* equip data */
    /* weapons */
    ptr += 4 * 6;
    /* arrows and bolts */
    ptr += 4 * 4;
    ptr += 4 * 2;
    /* armors */
    ptr += 4 * 4;
    ptr += 4;
    /* accessories */
    ptr += 4 * 4;
    ptr += 4;

    /* ChrAsm */
    ptr += 4 * 29;

    /* ChrAsm2 */
    ptr += 4 * 22;

    /* inventory 1 */
    /* count 1 */
    ptr += 4;
    /* part 1 */
    ptr += 0x0C * 0xA80;
    /* count 2 */
    ptr += 4;
    /* part 2 */
    ptr += 0x0C * 0x180;
    /* next_equip_index */
    ptr += 4;
    /* next_acquisition_sort_id */
    ptr += 4;

    /* spells */
    ptr += 8 * 14;
    /* current spell slot */
    ptr += 4;

    /* quick item slot: item_id + equipment_index */
    ptr += 8 * 10;
    /* current quick slot */
    ptr += 4;
    /* pouch item slot */
    ptr += 8 * 6;
    ptr += 8;

    /* Equpped geastures */
    ptr += 4 * 6;

    /* Projectile */
    /* count */
    if (ptr + 4 > end) return false;
    const uint32_t projectile_count = read_uint32(&ptr);
    if (projectile_count > (uint32_t)(end - ptr) / 8) return false;
    ptr += 8 * projectile_count;

    /* equipped items */
    ptr += 4 * 39;

    /* equip physics */
    ptr += 4 * 2;

    ptr += 4;

    ptr += 4;

    char_data->face_offset = (uint32_t)(ptr - char_data->data);
    /* face data */
    ptr += 0x120;

    ptr += 0x0B;

    /* inventory 2 */
    ptr += 4;
    ptr += 0x0C * 0x780;
    ptr += 4;
    ptr += 0x0C * 0x80;
    ptr += 4;
    ptr += 4;

    /* geastures */
    ptr += 4 * 0x40;

    /* regions */
    if (ptr + 4 > end) return false;
    const uint32_t region_count = read_uint32(&ptr);
    if (region_count > (uint32_t)(end - ptr) / 4) return false;
    ptr += 4 * region_count;

    /* rides */
    ptr += 0x28;

    ptr += 0x4D;

    /* menu profile save load */
    ptr += 0x1008;

    /* trophy equip data */
    ptr += 0x34;

    /* item seen list */
    ptr += 4;
    ptr += 4;
    ptr += 0x10 * 7000;

    /* tutorial data */
    ptr += 0x408;

    ptr += 0x1d;

    /* flags */
    ptr += 0x1bf99f;

    ptr += 1;

    for (int i = 0; i < 5; i++) {
        if (ptr + 4 > end) return false;
        const uint32_t sz = read_uint32(&ptr);
        if (sz > (uint32_t)(end - ptr)) return false;
        ptr += sz;
    }

    /* player coords */
    /* position (x, y, z, map_id) */
    ptr += 0x10;
    ptr += 0x11;
    /* position2 (x, y, z) */
    ptr += 0xC;
    ptr += 0x10;

    ptr += 0xF;

    /* account active  2=active  0=empty */
    ptr += 4;

    /* net data */
    ptr += 0x20000;

    /* weather info */
    ptr += 4 * 6;

    ptr += 0x10;

    char_data->userid_offset = (uint32_t)(ptr - char_data->data);

    /* CPS5Activity #1#

    ptr += 0x20;
     * dlc data #1#
    ptr += 0x32;

    ptr += 0x80;
    */

    /* all rest data */
    return true;
}

/**
 * @brief Reads character slot data from the save file
 * @param char_data Pointer to character data structure to fill
 * @param file Handle to the open save file
 * @return true if read successful, false otherwise
 */
static bool read_char_slot(er_char_data_t *char_data, HANDLE file) {
    if (SetFilePointer(file, char_data->slot_offset + 0x10, NULL, FILE_BEGIN) != char_data->slot_offset + 0x10) {
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
    if (SetFilePointer(file, summary_data->slot_offset + 0x10, NULL, FILE_BEGIN) != summary_data->slot_offset + 0x10) {
        return false;
    }
    DWORD bytes_read;
    if (!ReadFile(file, summary_data->data, sizeof(summary_data->data), &bytes_read, NULL) || bytes_read != sizeof(summary_data->data)) {
        return false;
    }

    const uint8_t *ptr = summary_data->data + 4;
    /* userid */
    ptr += 8;
    ptr += 0x140 + 4;
    const uint32_t sz = read_uint32(&ptr);
    const uint8_t *ptr2 = ptr + 4;
    if (read_uint32(&ptr2) != 0x11D0) {
        return false;
    }
    summary_data->face_offset = (uint32_t)(ptr2 - summary_data->data);
    ptr2 += 0x11D0;
    ptr2 += 8;
    summary_data->active_offset = (uint32_t)(ptr2 - summary_data->data);
    ptr += sz;
    summary_data->available_offset = (uint32_t)(ptr - summary_data->data);
    ptr += 10;
    summary_data->profile_offset = (uint32_t)(ptr - summary_data->data);
    return true;
}

er_save_data_t *er_save_data_load(const wchar_t *path) {
    er_save_data_t *save_data = LocalAlloc(LMEM_FIXED, sizeof(er_save_data_t));
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
    uint8_t header[0x300];
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    if (!ReadFile(file, header, sizeof(header), &bytes_read, NULL) || bytes_read != sizeof(header)) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }
    int save_slot_count = *(int *)&header[0x0C];
    if (save_slot_count < 12) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }

    uint32_t save_slot_size = *(uint32_t *)(&header[0x48 + 10 * 0x20]);
    if (save_slot_size != 0x60010) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }
    save_data->summary_data.slot_offset = *(uint32_t *)(&header[0x50 + 10 * 0x20]);
    if (!read_summary_slot(&save_data->summary_data, file)) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }

    for (int i = 0; i < 10; i++) {
        ZeroMemory(&save_data->char_data[i], sizeof(er_char_data_t));
        save_slot_size = *(uint32_t *)(&header[0x48 + i * 0x20]);
        if (save_slot_size != 0x280010) {
            LocalFree(save_data);
            CloseHandle(file);
            return NULL;
        }
        save_data->char_data[i].slot_offset = *(uint32_t *)(&header[0x50 + i * 0x20]);
        if (save_data->summary_data.data[save_data->summary_data.available_offset + i]) {
            if (!read_char_slot(&save_data->char_data[i], file)) {
                LocalFree(save_data);
                CloseHandle(file);
                return NULL;
            }
            CopyMemory(save_data->char_data[i].profile, save_data->summary_data.data + save_data->summary_data.profile_offset + 0x24C * i, 0x24C);
        }
    }

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
    uint8_t header[0x300];
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    if (!ReadFile(file, header, sizeof(header), &bytes_read, NULL) || bytes_read != sizeof(header)) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }
    int save_slot_count = *(int *)&header[0x0C];
    if (save_slot_count < 12) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }

    for (int i = 0; i < 10; i++) {
        save_data->slot_offset[i] = *(uint32_t *)(&header[0x50 + i * 0x20]);
    }

    uint32_t save_slot_size = *(uint32_t *)(&header[0x48 + 10 * 0x20]);
    if (save_slot_size != 0x60010) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }
    er_summary_data_t summary_data;
    summary_data.slot_offset = save_data->summary_slot_offset = *(uint32_t *)(&header[0x50 + 10 * 0x20]);
    if (!read_summary_slot(&summary_data, file)) {
        LocalFree(save_data);
        CloseHandle(file);
        return NULL;
    }
    save_data->summary_profile_offset = summary_data.profile_offset;
    const uint8_t *available_ptr = summary_data.data + summary_data.available_offset;
    for (int i = 0; i < 10; i++) {
        if (available_ptr[i]) {
            lstrcpyW(save_data->char_name[i], (wchar_t *)(summary_data.data + summary_data.profile_offset + 0x24C * i));
        } else {
            save_data->char_name[i][0] = 0;
        }
    }

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
    if (slot < 0 || slot >= 10) {
        return NULL;
    }
    return save_data->char_name[slot];
}

uint8_t *er_save_simple_data_slot_export(const er_save_simple_data_t *save_data, int slot) {
    if (slot < 0 || slot >= 10) {
        return NULL;
    }
    HANDLE file = CreateFileW(save_data->full_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    uint8_t *slot_data = LocalAlloc(LMEM_FIXED, 0x280000 + 0x24C);
    if (!slot_data) {
        CloseHandle(file);
        return NULL;
    }
    if (SetFilePointer(file, save_data->slot_offset[slot] + 0x10, NULL, FILE_BEGIN) != save_data->slot_offset[slot] + 0x10) {
        LocalFree(slot_data);
        CloseHandle(file);
        return NULL;
    }
    DWORD bytes_read;
    if (!ReadFile(file, slot_data, 0x280000, &bytes_read, NULL) || bytes_read != 0x280000) {
        LocalFree(slot_data);
        CloseHandle(file);
        return NULL;
    }
    if (SetFilePointer(file, save_data->summary_slot_offset + 0x10 + save_data->summary_profile_offset + 0x24C * slot, NULL, FILE_BEGIN) != save_data->summary_slot_offset + 0x10 + save_data->summary_profile_offset + 0x24C * slot) {
        LocalFree(slot_data);
        CloseHandle(file);
        return NULL;
    }
    if (!ReadFile(file, slot_data + 0x280000, 0x24C, &bytes_read, NULL) || bytes_read != 0x24C) {
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
        ok = ok && write_at(file, char_data->slot_offset + 0x10 + char_data->userid_offset, &user_id, sizeof(user_id));
    }
    *(uint64_t *)(save_data->summary_data.data + 4) = user_id;
    md5_buffer(save_data->summary_data.data, sizeof(save_data->summary_data.data), md5);
    ok = ok && write_at(file, save_data->summary_data.slot_offset, md5, sizeof(md5));
    ok = ok && write_at(file, save_data->summary_data.slot_offset + 0x10 + 0x04, &user_id, sizeof(user_id));
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
    if (slot < 0 || slot >= 10 || !char_data) {
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
    CopyMemory(summary_data->data + summary_data->profile_offset + 0x24C * slot, char_data->profile, 0x24C);
    *(summary_data->data + summary_data->available_offset + slot) = 1;

    /* Update userid */
    *(uint64_t *)(data->data + data->userid_offset) = *(uint64_t *)(summary_data->data + 0x04);

    uint8_t md5[0x10];
    md5_buffer(data->data, sizeof(data->data), md5);
    bool ok = write_at(file, data->slot_offset, md5, sizeof(md5));
    ok = ok && write_at(file, data->slot_offset + 0x10, data->data, sizeof(data->data));

    md5_buffer(summary_data->data, sizeof(summary_data->data), md5);
    ok = ok && write_at(file, summary_data->slot_offset, md5, sizeof(md5));
    ok = ok && write_at(file, summary_data->slot_offset + 0x10 + summary_data->profile_offset + 0x24C * slot, char_data->profile, 0x24C);
    const uint8_t byte_avail = 1;
    ok = ok && write_at(file, summary_data->slot_offset + 0x10 + summary_data->available_offset + slot, &byte_avail, sizeof(byte_avail));

    CloseHandle(file);
    return ok;
}

bool er_char_data_import_raw(er_save_data_t *save_data, int slot, const uint8_t *raw_data) {
    if (slot < 0 || slot >= 10 || !raw_data) {
        return false;
    }
    er_char_data_t *new_char = er_char_data_from_memory(raw_data);
    bool result = er_char_data_import(save_data, slot, new_char);
    er_char_data_free(new_char);
    return result;
}

bool er_char_data_set_name(er_save_data_t *save_data, int slot, const wchar_t *name) {
    if (slot < 0 || slot >= 10 || !name) {
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
    ZeroMemory(char_data->profile, 0x22);
    lstrcpynW((wchar_t *)(char_data->profile + 0x22), name, 0x11);
    ZeroMemory(char_data->data + char_data->stats_offset + 4 * 37, 0x22);
    lstrcpynW((wchar_t *)(char_data->data + char_data->stats_offset + 4 * 37), name, 0x11);
    ZeroMemory(summary_data->data + summary_data->profile_offset + 0x24C * slot, 0x22);
    lstrcpynW((wchar_t *)(summary_data->data + summary_data->profile_offset + 0x24C * slot), name, 0x11);

    uint8_t md5[0x10];
    md5_buffer(char_data->data, sizeof(char_data->data), md5);
    bool ok = write_at(file, char_data->slot_offset, md5, sizeof(md5));
    ok = ok && write_at(file, char_data->slot_offset + 0x10 + char_data->stats_offset + 4 * 37,
                        char_data->data + char_data->stats_offset + 4 * 37, 0x22);

    md5_buffer(summary_data->data, sizeof(summary_data->data), md5);
    ok = ok && write_at(file, summary_data->slot_offset, md5, sizeof(md5));
    ok = ok && write_at(file, summary_data->slot_offset + 0x10 + summary_data->profile_offset + 0x24C * slot,
                        summary_data->data + summary_data->profile_offset + 0x24C * slot, 0x22);

    CloseHandle(file);
    return ok;
}


const wchar_t *er_char_data_get_name(const er_char_data_t *char_data) {
    if (!char_data) {
        return NULL;
    }
    return (const wchar_t *)(char_data->data + char_data->stats_offset + 4 * 37);
}

bool er_char_data_info(const er_char_data_t *char_data, uint32_t *in_game_time, uint8_t *body_type, int *level, int stats[8]) {
    if (!char_data) {
        return false;
    }
    *in_game_time = *(uint32_t *)(char_data->data + 8);
    const uint32_t *stats_ptr = (const uint32_t *)(char_data->data + char_data->stats_offset + 4 * 13);
    CopyMemory(stats, stats_ptr, 32);
    *level = *(uint32_t *)(char_data->data + char_data->stats_offset + 4 * 24);
    *body_type = *(char_data->data + char_data->stats_offset + 0x94 + 0x22);
    return true;
}

er_char_data_t *er_char_data_from_file(const wchar_t *path) {
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
    if (!ReadFile(file, char_data->profile, 0x24C, &bytes_read, NULL) || bytes_read != 0x24C) {
        LocalFree(char_data);
        CloseHandle(file);
        return NULL;
    }
    CloseHandle(file);

    return char_data;
}

er_char_data_t *er_char_data_from_memory(const uint8_t *data) {
    er_char_data_t *char_data = LocalAlloc(LMEM_FIXED, sizeof(er_char_data_t));
    if (!char_data) {
        return NULL;
    }
    CopyMemory(char_data->data, data, 0x280000);
    if (!parse_char_slot(char_data)) {
        LocalFree(char_data);
        return NULL;
    }
    CopyMemory(char_data->profile, data + 0x280000, 0x24C);
    return char_data;
}

bool er_char_data_to_file(const er_char_data_t *char_data, const wchar_t *path) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written;
    bool ok = WriteFile(file, char_data->data, sizeof(char_data->data), &written, NULL) && written == sizeof(char_data->data);
    ok = ok && WriteFile(file, char_data->profile, 0x24C, &written, NULL) && written == 0x24C;
    CloseHandle(file);
    return ok;
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
    return summary_data->data + summary_data->face_offset + 0x130 * slot;
}

bool er_face_data_import(er_save_data_t *save_data, int slot, const uint8_t *face_data) {
    if (slot < 0 || slot >= 15 || !validate_face_data(face_data)) {
        return false;
    }
    er_summary_data_t *summary_data = &save_data->summary_data;
    CopyMemory(summary_data->data + summary_data->face_offset + 0x130 * slot, face_data, 0x130);
    uint8_t md5[0x10];
    md5_buffer(summary_data->data, sizeof(summary_data->data), md5);

    HANDLE file = CreateFileW(save_data->full_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = write_at(file, summary_data->slot_offset, md5, sizeof(md5));
    ok = ok && write_at(file, summary_data->slot_offset + 0x10 + summary_data->face_offset + 0x130 * slot,
                        summary_data->data + summary_data->face_offset + 0x130 * slot, 0x130);
    CloseHandle(file);
    return ok;
}

void er_face_data_info(const uint8_t *face_data, uint8_t *available, uint8_t *gender) {
    if (!face_data) {
        return;
    }
    *available = face_data[0x0];
    *gender = face_data[0x1];
}

uint8_t *er_face_data_from_file(const wchar_t *path) {
    uint8_t *face_data = LocalAlloc(LMEM_FIXED, 0x130);
    if (!face_data) {
        return NULL;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        LocalFree(face_data);
        return NULL;
    }
    DWORD bytes_read;
    if (!ReadFile(file, face_data, 0x130, &bytes_read, NULL) || bytes_read != 0x130) {
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
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written;
    bool ok = WriteFile(file, face_data, 0x130, &written, NULL) && written == 0x130;
    CloseHandle(file);
    return ok;
}

void er_face_data_free(uint8_t *face_data) {
    if (face_data) {
        LocalFree(face_data);
    }
}
