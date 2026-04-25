#pragma once

/**
 * @file bnd4_test_format.h
 * @brief BND4 format constants for selftest fixture builder
 * @details
 * Named constants for the hex literals used in praxis_make_min_valid_sl2()
 * to create minimal valid Elden Ring save files for testing. These constants
 * mirror the BND4 container format and ER save structure offsets.
 * For selftest use only; not part of production save handling.
 */

#include <stdint.h>

/* ER save file slot sizes */
#define BND4_TEST_CHAR_SLOT_SIZE    0x280010u  /* ER_CHAR_SLOT_FILE_SIZE */
#define BND4_TEST_SUMMARY_SLOT_SIZE 0x60010u   /* ER_SUMMARY_SLOT_FILE_SIZE */
#define BND4_TEST_SUMMARY_DATA_SIZE 0x60000u   /* ER_SUMMARY_DATA_SIZE */

/* ER save file offsets and layout */
#define BND4_TEST_FILE_HEADER_SIZE       0x300u   /* ER_FILE_HEADER_SIZE */
#define BND4_TEST_SUMMARY_FACE_SECTION   0x11D0u  /* ER_SUMMARY_FACE_SECTION_SIZE */

/* BND4 container structure offsets */
#define BND4_TEST_SLOT_COUNT_OFFSET      0x0Cu    /* Offset to slot count field in BND4 header */
#define BND4_TEST_SLOT_SIZE_ARRAY_OFFSET 0x48u    /* Offset to slot size array in BND4 header */
#define BND4_TEST_SLOT_OFFSET_ARRAY_OFFSET 0x50u  /* Offset to slot offset array in BND4 header */
#define BND4_TEST_SLOT_ENTRY_STRIDE      0x20u    /* Stride between slot entries in BND4 arrays */

/* Summary payload structure offsets (within slot data, after MD5 header) */
#define BND4_TEST_MD5_HEADER_SIZE        0x10u    /* Size of MD5 checksum prefix in slot */
#define BND4_TEST_SUMMARY_USER_ID_OFFSET 0x04u    /* Offset to user_id field in summary payload */
#define BND4_TEST_SUMMARY_SZ_OFFSET      0x150u   /* Offset to sz field (face + active + padding) */
#define BND4_TEST_SUMMARY_FACE_OFFSET    0x158u   /* Offset to face-section size marker */

/* Summary layout calculation */
#define BND4_TEST_SUMMARY_LAYOUT_ADJUSTMENT 0x14u /* Face section + padding adjustment */
