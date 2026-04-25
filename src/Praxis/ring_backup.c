/**
 * @file ring_backup.c
 * @brief Ring backup implementation.
 */

#include "ring_backup.h"

#include <windows.h>
#include <shlwapi.h>

static wchar_t g_ring_dir[MAX_PATH];
static int g_ring_size = 5;

/**
 * @brief List ring backup files in directory, sorted by name (descending = newest first).
 * @param ring_dir Directory to search (e.g., .praxis_ring/)
 * @param out_paths Array of MAX_PATH buffers to populate with full paths
 * @param out_capacity Maximum number of paths to return
 * @return Number of files found (0 if none), or -1 on error.
 *         Closes FindFirstFile/FindNextFile handles on every path.
 */
static int ring_list_files(const wchar_t *ring_dir, wchar_t out_paths[][MAX_PATH], int out_capacity) {
    if (!ring_dir || !out_paths || out_capacity <= 0) return -1;

    wchar_t search[MAX_PATH];
    lstrcpyW(search, ring_dir);
    PathAppendW(search, L"ring_*.ersm");

    wchar_t temp_files[64][MAX_PATH];
    int count = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        if (count < 64) {
            lstrcpyW(temp_files[count], ring_dir);
            PathAppendW(temp_files[count], fd.cFileName);
            count++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    /* Sort lexically (timestamp prefix = chronological), then reverse for descending */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (lstrcmpW(temp_files[i], temp_files[j]) < 0) {
                wchar_t tmp[MAX_PATH];
                lstrcpyW(tmp, temp_files[i]);
                lstrcpyW(temp_files[i], temp_files[j]);
                lstrcpyW(temp_files[j], tmp);
            }
        }
    }

    /* Copy to output, respecting capacity */
    int copy_count = count < out_capacity ? count : out_capacity;
    for (int i = 0; i < copy_count; i++) {
        lstrcpyW(out_paths[i], temp_files[i]);
    }
    return copy_count;
}

bool ring_backup_init(const wchar_t *tree_root, int ring_size) {
    lstrcpyW(g_ring_dir, tree_root);
    PathAppendW(g_ring_dir, PRAXIS_RING_DIR_NAME);
    g_ring_size = ring_size > 0 ? ring_size : 5;
    if (!CreateDirectoryW(g_ring_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    SetFileAttributesW(g_ring_dir, FILE_ATTRIBUTE_HIDDEN);
    return true;
}

bool ring_backup_snapshot(const game_backend_t *backend, const wchar_t *current_save,
                          const wchar_t *operation_label, int compression_level,
                          wchar_t *out_backup_path, size_t out_chars) {
    if (!backend || !current_save || !operation_label) return false;

    /* Build timestamp-based filename: ring_YYYYMMDDHHMMSSmmm_<label>.ersm
     * Milliseconds prevent collisions when two snapshots occur in the same
     * second (e.g. restore immediately followed by undo). */
    SYSTEMTIME st;
    GetSystemTime(&st);
    wchar_t filename[MAX_PATH];
    _snwprintf(filename, MAX_PATH, L"ring_%04d%02d%02d%02d%02d%02d%03d_%ls.ersm",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        st.wMilliseconds, operation_label);

    wchar_t dst[MAX_PATH];
    lstrcpyW(dst, g_ring_dir);
    PathAppendW(dst, filename);

    /* Rotate: delete oldest if count >= ring_size */
    {
        wchar_t ring_files[64][MAX_PATH];
        int ring_count = ring_list_files(g_ring_dir, ring_files, 64);
        if (ring_count < 0) ring_count = 0;

        /* Delete oldest (last in descending-sorted list) until count < ring_size */
        while (ring_count >= g_ring_size) {
            DeleteFileW(ring_files[ring_count - 1]);
            ring_count--;
        }
    }

    /* Use raw file copy rather than backend->backup_full so that ring snapshots
     * preserve the current save byte-for-byte, including corrupt or non-BND4
     * content that the backend might reject. This keeps abort-on-ring-fail a
     * true file-system-error-only path. compression_level is unused here. */
    (void)compression_level;
    if (!CopyFileW(current_save, dst, FALSE)) return false;

    if (out_backup_path && out_chars > 0) {
        if ((size_t)lstrlenW(dst) < out_chars) lstrcpyW(out_backup_path, dst);
    }
    return true;
}

bool ring_backup_get_latest(const wchar_t *tree_root, wchar_t *out_path, size_t out_chars) {
    wchar_t ring_dir[MAX_PATH];
    lstrcpyW(ring_dir, tree_root);
    PathAppendW(ring_dir, PRAXIS_RING_DIR_NAME);

    wchar_t ring_files[64][MAX_PATH];
    int ring_count = ring_list_files(ring_dir, ring_files, 64);
    if (ring_count <= 0) return false;

    /* First file is newest (descending sort) */
    if ((size_t)lstrlenW(ring_files[0]) >= out_chars) return false;
    lstrcpyW(out_path, ring_files[0]);
    return true;
}
