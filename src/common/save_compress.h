/**
 * @file save_compress.h
 * @brief ERSM compressed container format over LZMA SDK
 * @details Public API for compressing and decompressing character-slot and
 *          full-save data using the custom ERSM container format.
 */

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

/* ERSM container data-type tags */
#define ERSM_TYPE_CHAR_SLOT  0x01u
#define ERSM_TYPE_FULL_SAVE  0x02u

/* Hard upper bound on decompressed payload (128 MB). Guards against
 * malicious headers that would otherwise trigger huge allocations. */
#define ERSM_MAX_UNCOMPRESSED_SIZE (128u * 1024u * 1024u)

/* Compression-level presets exposed by the UI. */
#define ERSM_LEVEL_FAST   1
#define ERSM_LEVEL_NORMAL 5
#define ERSM_LEVEL_MAX    9

typedef enum ersm_format_e {
    ERSM_FMT_UNKNOWN = 0,
    ERSM_FMT_ERSM_CONTAINER,
    ERSM_FMT_BND4_RAW
} ersm_format_t;

/**
 * @brief One-shot LZMA SDK bootstrap; call once at app start.
 * @details Calls LzFindPrepare() for SIMD speedup. Idempotent.
 */
void save_compress_init(void);

/**
 * @brief Detect the format of a file by reading its first 4 bytes.
 * @param path Wide-character file path
 * @return ERSM_FMT_ERSM_CONTAINER, ERSM_FMT_BND4_RAW, or ERSM_FMT_UNKNOWN
 */
ersm_format_t ersm_detect_file_format(const wchar_t *path);

/**
 * @brief Compress a buffer to an ERSM container file.
 * @param path       Destination file path
 * @param src        Source data buffer
 * @param src_len    Source data length in bytes
 * @param data_type  ERSM_TYPE_CHAR_SLOT or ERSM_TYPE_FULL_SAVE
 * @param level      LZMA compression level 0..9
 * @return true on success, false on any failure
 */
bool ersm_compress_to_file(const wchar_t *path,
                           const uint8_t *src, size_t src_len,
                           uint8_t data_type, int level);

/**
 * @brief Decompress an ERSM container file to a heap-allocated buffer.
 * @param path      Source ERSM file path
 * @param out_size  Receives decompressed payload size
 * @param out_type  Receives data_type byte from header
 * @return LocalAlloc'd buffer (caller must LocalFree), or NULL on failure
 */
uint8_t *ersm_decompress_from_file(const wchar_t *path,
                                   size_t *out_size, uint8_t *out_type);

/**
 * @brief Decompress an ERSM container to a temporary file.
 * @param src_path      Source ERSM file path
 * @param out_temp_path Buffer (MAX_PATH) that receives the temp file path
 * @param out_type      Receives data_type byte from header
 * @return true on success (caller must DeleteFileW out_temp_path), false on failure
 */
bool ersm_decompress_to_temp_file(const wchar_t *src_path,
                                   wchar_t *out_temp_path,
                                   uint8_t *out_type);

/**
 * @brief Write raw BND4 file (no ERSM wrapper, no compression).
 * @details Used by full-save backups when compression_level == COMP_LEVEL_NONE.
 *          The destination file becomes a byte-identical copy of the source.
 *          Restore handles this transparently via ersm_detect_file_format()
 *          returning ERSM_FMT_BND4_RAW. The on-disk extension is chosen by
 *          the caller (the Praxis tool now uses `.ersm` uniformly).
 * @param path Destination file path
 * @param src Source bytes (must start with "BND4" magic)
 * @param src_len Source data length
 * @return true on success, false on magic validation failure or file I/O error
 */
bool ersm_write_raw_bnd4_to_file(const wchar_t *path, const uint8_t *src, size_t src_len);

typedef enum save_kind_e {
    SAVE_KIND_UNKNOWN  = 0,
    SAVE_KIND_FULL     = 1,  /* ERSM_FMT_BND4_RAW or ERSM with data_type=ERSM_TYPE_FULL_SAVE */
    SAVE_KIND_SLOT     = 2   /* ERSM with data_type=ERSM_TYPE_CHAR_SLOT */
} save_kind_t;

/**
 * @brief Classify a backup file as full-save, slot-save, or unknown.
 * @details Reads up to 21 bytes from the file. Uses ersm_detect_file_format,
 *          then for ERSM containers reads the data_type byte at offset 5.
 *          Returns SAVE_KIND_UNKNOWN on file errors, truncated header,
 *          or unrecognized format.
 * @param path Wide-character path to the backup file
 * @return SAVE_KIND_FULL, SAVE_KIND_SLOT, or SAVE_KIND_UNKNOWN
 */
save_kind_t save_compress_classify_backup(const wchar_t *path);
