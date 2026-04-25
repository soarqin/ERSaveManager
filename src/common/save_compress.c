/**
 * @file save_compress.c
 * @brief Implementation of the ERSM compressed container format over the LZMA SDK
 * @details This file contains the implementation of functions for compressing and decompressing
 *          character-slot and full-save data using the custom ERSM container format.
 */

#include "save_compress.h"

#include <LzmaEnc.h>
#include <LzmaDec.h>
#include <Alloc.h>
#include <LzFind.h>

#include <stdint.h>
#include <string.h>

#include <windows.h>
#include <shlwapi.h>

#define ERSM_MAGIC         "ERSM"
#define ERSM_MAGIC_LEN     4
#define ERSM_VERSION       1u
#define ERSM_HEADER_SIZE   21
#define LZMA_PROPS_SIZE    5

/*
 * Byte  0.. 3: magic "ERSM"
 * Byte  4:     version (must be 1)
 * Byte  5:     data_type (ERSM_TYPE_CHAR_SLOT or ERSM_TYPE_FULL_SAVE)
 * Bytes 6.. 7: reserved (zero on write, ignored on read)
 * Bytes 8..15: uncompressed_size (uint64 little-endian)
 * Bytes16..20: LZMA props (5 bytes)
 */

static bool read_exact(HANDLE f, uint8_t *buf, DWORD n) {
    DWORD total = 0;
    while (total < n) {
        DWORD chunk = 0;
        if (!ReadFile(f, buf + total, n - total, &chunk, NULL) || chunk == 0) return false;
        total += chunk;
    }
    return true;
}

static bool write_exact(HANDLE f, const uint8_t *buf, DWORD n) {
    DWORD written = 0;
    if (!WriteFile(f, buf, n, &written, NULL) || written != n) return false;
    return true;
}

static bool parse_header(const uint8_t *hdr, uint8_t *out_type, uint64_t *out_size, uint8_t out_props[5]) {
    uint64_t size = 0;

    if (memcmp(hdr, ERSM_MAGIC, ERSM_MAGIC_LEN) != 0) return false;
    if (hdr[4] != ERSM_VERSION) return false;

    for (int i = 0; i < 8; i++) {
        size |= ((uint64_t)hdr[8 + i]) << (8 * i);
    }

    *out_type = hdr[5];
    *out_size = size;
    CopyMemory(out_props, hdr + 16, LZMA_PROPS_SIZE);
    return true;
}

static bool write_header(HANDLE f, uint8_t data_type, uint64_t payload_size, const uint8_t props[5]) {
    uint8_t hdr[ERSM_HEADER_SIZE];

    CopyMemory(hdr, ERSM_MAGIC, ERSM_MAGIC_LEN);
    hdr[4] = ERSM_VERSION;
    hdr[5] = data_type;
    hdr[6] = 0;
    hdr[7] = 0;
    for (int i = 0; i < 8; i++) {
        hdr[8 + i] = (uint8_t)((payload_size >> (8 * i)) & 0xFFu);
    }
    CopyMemory(hdr + 16, props, LZMA_PROPS_SIZE);

    return write_exact(f, hdr, ERSM_HEADER_SIZE);
}

void save_compress_init(void) {
    static int initialized = 0;
    if (!initialized) {
        LzFindPrepare();
        initialized = 1;
    }
}

ersm_format_t ersm_detect_file_format(const wchar_t *path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return ERSM_FMT_UNKNOWN;
    uint8_t magic[4];
    DWORD read_bytes = 0;
    BOOL ok = ReadFile(f, magic, 4, &read_bytes, NULL);
    CloseHandle(f);
    if (!ok || read_bytes < 4) return ERSM_FMT_UNKNOWN;
    if (memcmp(magic, "ERSM", 4) == 0) return ERSM_FMT_ERSM_CONTAINER;
    if (memcmp(magic, "BND4", 4) == 0) return ERSM_FMT_BND4_RAW;
    return ERSM_FMT_UNKNOWN;
}

bool ersm_compress_to_file(const wchar_t *path, const uint8_t *src, size_t src_len, uint8_t data_type, int level) {
    uint8_t *dest;
    SIZE_T dest_capacity;
    SizeT dest_len;
    CLzmaEncProps props;
    uint8_t props_encoded[LZMA_PROPS_SIZE];
    size_t props_size = LZMA_PROPS_SIZE;
    SRes res;
    HANDLE f;

    if (!path || !src || src_len == 0) {
        return false;
    }

    dest_capacity = (SIZE_T)(LZMA_PROPS_SIZE + src_len + (src_len / 3) + 128);
    dest = LocalAlloc(LMEM_FIXED, dest_capacity);
    if (!dest) {
        DeleteFileW(path);
        return false;
    }

    LzmaEncProps_Init(&props);
    props.level = (level < 0 ? 0 : (level > 9 ? 9 : level));
    props.reduceSize = (UInt64)src_len;

    dest_len = (SizeT)dest_capacity;
    res = LzmaEncode(dest, &dest_len, src, (SizeT)src_len, &props, props_encoded, &props_size,
                     0, NULL, &g_Alloc, &g_Alloc);
    if (res != SZ_OK || props_size != LZMA_PROPS_SIZE) {
        LocalFree(dest);
        DeleteFileW(path);
        return false;
    }

    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        LocalFree(dest);
        DeleteFileW(path);
        return false;
    }

    if (!write_header(f, data_type, (uint64_t)src_len, props_encoded)) {
        CloseHandle(f);
        LocalFree(dest);
        DeleteFileW(path);
        return false;
    }

    if (!write_exact(f, dest, (DWORD)dest_len)) {
        CloseHandle(f);
        LocalFree(dest);
        DeleteFileW(path);
        return false;
    }

    CloseHandle(f);
    LocalFree(dest);
    return true;
}

uint8_t *ersm_decompress_from_file(const wchar_t *path, size_t *out_size, uint8_t *out_type) {
    HANDLE f;
    uint8_t hdr[ERSM_HEADER_SIZE];
    uint8_t props[LZMA_PROPS_SIZE];
    uint8_t data_type = 0;
    uint64_t uncompressed_size = 0;
    LARGE_INTEGER file_size_li;
    LONGLONG file_size;
    LONGLONG stream_len;
    uint8_t *compressed;
    uint8_t *dest;
    SizeT dest_len;
    SizeT src_len;
    ELzmaStatus status;
    SRes sres;

    if (!path || !out_size) {
        return NULL;
    }

    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    if (!read_exact(f, hdr, ERSM_HEADER_SIZE)) {
        CloseHandle(f);
        return NULL;
    }

    if (!parse_header(hdr, &data_type, &uncompressed_size, props)) {
        CloseHandle(f);
        return NULL;
    }

    if (uncompressed_size == 0 || uncompressed_size > ERSM_MAX_UNCOMPRESSED_SIZE) {
        CloseHandle(f);
        return NULL;
    }

    /* Caller (game backend) is responsible for validating decompressed size
     * matches expected slot/full layout. */

    if (!GetFileSizeEx(f, &file_size_li)) {
        CloseHandle(f);
        return NULL;
    }

    file_size = file_size_li.QuadPart;
    stream_len = file_size - ERSM_HEADER_SIZE;
    if (stream_len <= 0 || stream_len >= (512LL * 1024 * 1024)) {
        CloseHandle(f);
        return NULL;
    }

    compressed = LocalAlloc(LMEM_FIXED, (SIZE_T)stream_len);
    if (!compressed) {
        CloseHandle(f);
        return NULL;
    }

    dest = LocalAlloc(LMEM_FIXED, (SIZE_T)uncompressed_size);
    if (!dest) {
        LocalFree(compressed);
        CloseHandle(f);
        return NULL;
    }

    if (!read_exact(f, compressed, (DWORD)stream_len)) {
        LocalFree(compressed);
        LocalFree(dest);
        CloseHandle(f);
        return NULL;
    }

    CloseHandle(f);

    dest_len = (SizeT)uncompressed_size;
    src_len = (SizeT)stream_len;
    sres = LzmaDecode(dest, &dest_len, compressed, &src_len,
                      props, LZMA_PROPS_SIZE,
                      LZMA_FINISH_END, &status, &g_Alloc);
    if (sres != SZ_OK ||
        dest_len != (SizeT)uncompressed_size ||
        src_len != (SizeT)stream_len ||
        (status != LZMA_STATUS_FINISHED_WITH_MARK &&
         status != LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK)) {
        LocalFree(compressed);
        LocalFree(dest);
        return NULL;
    }

    LocalFree(compressed);
    if (out_type) *out_type = data_type;
    *out_size = (size_t)dest_len;
    return dest;
}

bool ersm_decompress_to_temp_file(const wchar_t *src_path, wchar_t *out_temp_path, uint8_t *out_type) {
    wchar_t tmp_dir[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tmp_dir)) return false;
    if (!GetTempFileNameW(tmp_dir, L"ersm", 0, out_temp_path)) return false;

    size_t out_size = 0;
    uint8_t local_type = 0;
    uint8_t *data = ersm_decompress_from_file(src_path, &out_size, &local_type);
    if (!data) {
        DeleteFileW(out_temp_path);
        return false;
    }

    HANDLE f = CreateFileW(out_temp_path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        LocalFree(data);
        DeleteFileW(out_temp_path);
        return false;
    }

    bool ok = write_exact(f, data, (DWORD)out_size);
    CloseHandle(f);
    LocalFree(data);

    if (!ok) {
        DeleteFileW(out_temp_path);
        return false;
    }

    if (out_type) *out_type = local_type;
    return true;
}

bool ersm_write_raw_bnd4_to_file(const wchar_t *path, const uint8_t *src, size_t src_len) {
    if (!path || !src || src_len < 4 || memcmp(src, "BND4", 4) != 0) {
        return false;
    }

    HANDLE fh = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    if (!WriteFile(fh, src, (DWORD)src_len, &written, NULL) || written != (DWORD)src_len) {
        CloseHandle(fh);
        DeleteFileW(path);
        return false;
    }

    CloseHandle(fh);
    return true;
}
