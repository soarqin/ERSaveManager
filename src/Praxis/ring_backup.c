/**
 * @file ring_backup.c
 * @brief Ring backup implementation.
 */

#include "ring_backup.h"

#include <windows.h>
#include <shlwapi.h>

static wchar_t g_ring_dir[MAX_PATH];
static int g_ring_size = 5;

bool ring_backup_init(const wchar_t *tree_root, int ring_size) {
    lstrcpyW(g_ring_dir, tree_root);
    PathAppendW(g_ring_dir, L".praxis_ring");
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
        wchar_t search[MAX_PATH];
        lstrcpyW(search, g_ring_dir);
        PathAppendW(search, L"ring_*.ersm");

        /* Collect all ring files */
        wchar_t ring_files[64][MAX_PATH];
        int ring_count = 0;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(search, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (ring_count < 64) {
                    lstrcpyW(ring_files[ring_count], g_ring_dir);
                    PathAppendW(ring_files[ring_count], fd.cFileName);
                    ring_count++;
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }

        /* Sort lexically (timestamp prefix = chronological) */
        for (int i = 0; i < ring_count - 1; i++) {
            for (int j = i + 1; j < ring_count; j++) {
                if (lstrcmpW(ring_files[i], ring_files[j]) > 0) {
                    wchar_t tmp[MAX_PATH];
                    lstrcpyW(tmp, ring_files[i]);
                    lstrcpyW(ring_files[i], ring_files[j]);
                    lstrcpyW(ring_files[j], tmp);
                }
            }
        }

        /* Delete oldest until count < ring_size */
        while (ring_count >= g_ring_size) {
            DeleteFileW(ring_files[0]);
            for (int i = 0; i < ring_count - 1; i++) lstrcpyW(ring_files[i], ring_files[i + 1]);
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
    PathAppendW(ring_dir, L".praxis_ring");

    wchar_t search[MAX_PATH];
    lstrcpyW(search, ring_dir);
    PathAppendW(search, L"ring_*.ersm");

    wchar_t latest[MAX_PATH] = {0};
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        wchar_t candidate[MAX_PATH];
        lstrcpyW(candidate, ring_dir);
        PathAppendW(candidate, fd.cFileName);
        if (lstrcmpW(candidate, latest) > 0) lstrcpyW(latest, candidate);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (latest[0] == L'\0') return false;
    if ((size_t)lstrlenW(latest) >= out_chars) return false;
    lstrcpyW(out_path, latest);
    return true;
}
