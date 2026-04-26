/**
 * @file save_watcher.c
 * @brief ReadDirectoryChangesW worker-thread watcher implementation for Praxis.
 */

#include "save_watcher.h"

#include <stddef.h>
#include <stdint.h>

struct save_watcher_s {
    HWND notify_hwnd;
    UINT message_id;
    HANDLE dir_handle;
    HANDLE thread_handle;
    HANDLE shutdown_event;
    HANDLE wakeup_event;
    CRITICAL_SECTION cs;
    wchar_t root_path[MAX_PATH];
    uint8_t buffer[64 * 1024];
    OVERLAPPED ovl;
    HANDLE ovl_event;
};

static HANDLE open_directory_handle(const wchar_t *root_path) {
    return CreateFileW(root_path,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL);
}

static void drain_overlapped_io(save_watcher_t *w) {
    DWORD bytes_transferred = 0;

    if (!w || w->dir_handle == INVALID_HANDLE_VALUE) {
        return;
    }

    CancelIoEx(w->dir_handle, &w->ovl);
    GetOverlappedResult(w->dir_handle, &w->ovl, &bytes_transferred, TRUE);
}

static bool copy_root_path(save_watcher_t *w, wchar_t *out_root, int out_chars) {
    if (!w || !out_root || out_chars == 0) {
        return false;
    }

    EnterCriticalSection(&w->cs);
    if (!lstrcpynW(out_root, w->root_path, out_chars)) {
        LeaveCriticalSection(&w->cs);
        return false;
    }
    LeaveCriticalSection(&w->cs);
    return true;
}

static DWORD WINAPI watcher_thread_proc(LPVOID param) {
    save_watcher_t *w = (save_watcher_t *)param;

    if (!w) {
        return 0;
    }

    while (1) {
        DWORD bytes_returned = 0;
        HANDLE events[3];
        DWORD wait_result;
        BOOL ok;

        if (w->dir_handle == INVALID_HANDLE_VALUE) {
            PostMessageW(w->notify_hwnd, w->message_id, 2, 0);
            break;
        }

        ZeroMemory(&w->ovl, sizeof(w->ovl));
        w->ovl.hEvent = w->ovl_event;
        ResetEvent(w->ovl_event);

        ok = ReadDirectoryChangesW(
            w->dir_handle,
            w->buffer,
            sizeof(w->buffer),
            TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
            NULL,
            &w->ovl,
            NULL);
        if (!ok && GetLastError() != ERROR_IO_PENDING) {
            PostMessageW(w->notify_hwnd, w->message_id, 2, 0);
            break;
        }

        events[0] = w->ovl_event;
        events[1] = w->shutdown_event;
        events[2] = w->wakeup_event;
        wait_result = WaitForMultipleObjects(3, events, FALSE, INFINITE);

        if (wait_result == WAIT_OBJECT_0 + 1) {
            drain_overlapped_io(w);
            break;
        }

        if (wait_result == WAIT_OBJECT_0 + 2) {
            wchar_t new_root[MAX_PATH];
            HANDLE new_handle;

            drain_overlapped_io(w);
            CloseHandle(w->dir_handle);
            w->dir_handle = INVALID_HANDLE_VALUE;

            if (!copy_root_path(w, new_root, MAX_PATH)) {
                PostMessageW(w->notify_hwnd, w->message_id, 2, 0);
                break;
            }

            new_handle = open_directory_handle(new_root);
            if (new_handle == INVALID_HANDLE_VALUE) {
                PostMessageW(w->notify_hwnd, w->message_id, 2, 0);
                break;
            }

            w->dir_handle = new_handle;
            continue;
        }

        if (wait_result != WAIT_OBJECT_0) {
            PostMessageW(w->notify_hwnd, w->message_id, 2, 0);
            break;
        }

        if (!GetOverlappedResult(w->dir_handle, &w->ovl, &bytes_returned, FALSE)) {
            PostMessageW(w->notify_hwnd, w->message_id, 2, 0);
            break;
        }

        if (bytes_returned == 0) {
            PostMessageW(w->notify_hwnd, w->message_id, 1, 0);
        } else {
            PostMessageW(w->notify_hwnd, w->message_id, 0, 0);
        }
    }

    return 0;
}

save_watcher_t *save_watcher_start(HWND notify_hwnd, const wchar_t *root_path, UINT message_id) {
    save_watcher_t *w;

    if (!notify_hwnd || !root_path || root_path[0] == L'\0' ||
        (size_t)lstrlenW(root_path) >= MAX_PATH) {
        return NULL;
    }

    w = LocalAlloc(LMEM_FIXED, sizeof(*w));
    if (!w) {
        return NULL;
    }
    ZeroMemory(w, sizeof(*w));

    w->notify_hwnd = notify_hwnd;
    w->message_id = message_id;
    w->dir_handle = INVALID_HANDLE_VALUE;
    if (!lstrcpynW(w->root_path, root_path, MAX_PATH)) {
        LocalFree(w);
        return NULL;
    }
    InitializeCriticalSection(&w->cs);

    w->shutdown_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!w->shutdown_event) {
        DeleteCriticalSection(&w->cs);
        LocalFree(w);
        return NULL;
    }

    w->wakeup_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!w->wakeup_event) {
        CloseHandle(w->shutdown_event);
        DeleteCriticalSection(&w->cs);
        LocalFree(w);
        return NULL;
    }

    w->ovl_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!w->ovl_event) {
        CloseHandle(w->wakeup_event);
        CloseHandle(w->shutdown_event);
        DeleteCriticalSection(&w->cs);
        LocalFree(w);
        return NULL;
    }

    w->dir_handle = open_directory_handle(root_path);
    if (w->dir_handle == INVALID_HANDLE_VALUE) {
        CloseHandle(w->ovl_event);
        CloseHandle(w->wakeup_event);
        CloseHandle(w->shutdown_event);
        DeleteCriticalSection(&w->cs);
        LocalFree(w);
        return NULL;
    }

    w->thread_handle = CreateThread(NULL, 0, watcher_thread_proc, w, 0, NULL);
    if (!w->thread_handle) {
        CloseHandle(w->dir_handle);
        CloseHandle(w->ovl_event);
        CloseHandle(w->wakeup_event);
        CloseHandle(w->shutdown_event);
        DeleteCriticalSection(&w->cs);
        LocalFree(w);
        return NULL;
    }

    return w;
}

void save_watcher_stop(save_watcher_t *w) {
    if (!w) {
        return;
    }

    if (w->shutdown_event) {
        SetEvent(w->shutdown_event);
    }

    if (w->dir_handle != INVALID_HANDLE_VALUE) {
        CancelIoEx(w->dir_handle, &w->ovl);
    }

    if (w->thread_handle) {
        WaitForSingleObject(w->thread_handle, INFINITE);
    }

    if (w->thread_handle) {
        CloseHandle(w->thread_handle);
    }
    if (w->dir_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(w->dir_handle);
    }
    if (w->shutdown_event) {
        CloseHandle(w->shutdown_event);
    }
    if (w->wakeup_event) {
        CloseHandle(w->wakeup_event);
    }
    if (w->ovl_event) {
        CloseHandle(w->ovl_event);
    }

    DeleteCriticalSection(&w->cs);
    LocalFree(w);
}

bool save_watcher_change_root(save_watcher_t *w, const wchar_t *new_root) {
    if (!w || !new_root || new_root[0] == L'\0' ||
        (size_t)lstrlenW(new_root) >= MAX_PATH) {
        return false;
    }

    EnterCriticalSection(&w->cs);
    if (!lstrcpynW(w->root_path, new_root, MAX_PATH)) {
        LeaveCriticalSection(&w->cs);
        return false;
    }
    LeaveCriticalSection(&w->cs);

    if (!w->wakeup_event) {
        return false;
    }

    return SetEvent(w->wakeup_event) != 0;
}
