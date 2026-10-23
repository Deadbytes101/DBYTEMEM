/* DBYTEMEM R6 native disk watch. CC0-1.0. no disk cleanup. */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <io.h>
#include "disk_policy.h"

enum { DBM_PATH_CAP = 768, DBM_EVENT_CAP = 128, DBM_EVENT_COUNT = 99 };
static const wchar_t *names[] = {L"C:\\", L"D:\\"};
static const char *labels[] = {"C:", "D:"};
static const char *status_name(int r)
{
    return r == 4 ? "CRITICAL" : r == 3 ? "WARN" : "OK";
}
static int path_join(wchar_t *out, size_t cap, const wchar_t *dir, const wchar_t *leaf)
{
    int n = swprintf(out, cap, L"%ls\\%ls", dir, leaf);
    return n > 0 && (size_t)n < cap;
}
static int ensure_dir(const wchar_t *dir)
{
    if (CreateDirectoryW(dir, NULL)) return 1;
    return GetLastError() == ERROR_ALREADY_EXISTS &&
           (GetFileAttributesW(dir) & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
static int state_read(const wchar_t *path, int *old)
{
    char text[64];
    FILE *f;
    DWORD err;
    *old = -1;
    if (_wfopen_s(&f, path, L"rb") != 0) f = NULL;
    if (!f) {
        err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND ? 1 : 0;
    }
    if (!fgets(text, sizeof text, f) || fgetc(f) != EOF || ferror(f)) {
        fclose(f);
        return 0;
    }
    if (fclose(f)) return 0;
    if (!strcmp(text, "DBYTEMEM_R6_V1 OK\n")) *old = 0;
    else if (!strcmp(text, "DBYTEMEM_R6_V1 WARN\n")) *old = 3;
    else if (!strcmp(text, "DBYTEMEM_R6_V1 CRITICAL\n")) *old = 4;
    else return 0;
    return 1;
}
static int write_atomic(const wchar_t *dest, const char *data, size_t size)
{
    wchar_t temp[DBM_PATH_CAP];
    FILE *f;
    int n = swprintf(temp, DBM_PATH_CAP, L"%ls.tmp.%lu", dest,
                     (unsigned long)GetCurrentProcessId());
    if (n <= 0 || n >= DBM_PATH_CAP) return 0;
    if (_wfopen_s(&f, temp, L"wb") != 0) f = NULL;
    if (!f) return 0;
    if (fwrite(data, 1, size, f) != size || fflush(f) ||
        _commit(_fileno(f))) {
        fclose(f);
        return 0;
    }
    if (fclose(f)) return 0;
    return MoveFileExW(temp, dest, MOVEFILE_REPLACE_EXISTING |
                       MOVEFILE_WRITE_THROUGH) != 0;
}
static int event_write(const wchar_t *path, int before, int after,
                       uint64_t free_c, uint64_t free_d)
{
    char entries[DBM_EVENT_COUNT][DBM_EVENT_CAP];
    char next[DBM_EVENT_CAP];
    char record[DBM_EVENT_CAP * (DBM_EVENT_COUNT + 1)];
    FILE *f;
    unsigned int n = 0, i;
    FILETIME ft;
    ULARGE_INTEGER stamp;
    int k;
    size_t pos = 0, len;
    if (_wfopen_s(&f, path, L"rb") != 0) f = NULL;
    if (f) {
        char line[DBM_EVENT_CAP];
        while (fgets(line, sizeof line, f)) {
            len = strlen(line);
            if (!len || line[len - 1] != '\n') {
                fclose(f);
                return 0;
            }
            if (n == DBM_EVENT_COUNT) {
                memmove(entries, entries + 1,
                        sizeof entries[0] * (DBM_EVENT_COUNT - 1));
                n = DBM_EVENT_COUNT - 1;
            }
            memcpy(entries[n++], line, len + 1);
        }
        if (ferror(f)) { fclose(f); return 0; }
        if (fclose(f)) return 0;
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    GetSystemTimeAsFileTime(&ft);
    stamp.LowPart = ft.dwLowDateTime;
    stamp.HighPart = ft.dwHighDateTime;
    k = snprintf(next, sizeof next, "%llu,%s,%s,%llu,%llu\n",
                 (unsigned long long)stamp.QuadPart,
                 before < 0 ? "UNKNOWN" : status_name(before),
                 status_name(after), (unsigned long long)free_c,
                 (unsigned long long)free_d);
    if (k <= 0 || k >= (int)sizeof next) return 0;
    for (i = 0; i < n; ++i) {
        len = strlen(entries[i]);
        if (len >= sizeof record - pos) return 0;
        memcpy(record + pos, entries[i], len);
        pos += len;
    }
    if ((size_t)k > sizeof record - pos) return 0;
    memcpy(record + pos, next, (size_t)k);
    pos += (size_t)k;
    return write_atomic(path, record, pos);
}
static int tray_warn(int rank, uint64_t c, uint64_t d)
{
    HWND wnd;
    NOTIFYICONDATAW ni;
    BOOL added;
    wnd = CreateWindowExW(0, L"STATIC", L"DBYTEMEM", WS_OVERLAPPED,
                          0, 0, 0, 0, NULL, NULL,
                          GetModuleHandleW(NULL), NULL);
    if (!wnd) return 0;
    ZeroMemory(&ni, sizeof ni);
    ni.cbSize = sizeof ni;
    ni.hWnd = wnd;
    ni.uID = 1;
    ni.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
    ni.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32515));
    ni.dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(ni.szTip, sizeof ni.szTip / sizeof ni.szTip[0], L"DBYTEMEM disk watch");
    wcscpy_s(ni.szInfoTitle, sizeof ni.szInfoTitle / sizeof ni.szInfoTitle[0],
             rank == 4 ? L"DBYTEMEM disk CRITICAL" : L"DBYTEMEM disk WARN");
    swprintf(ni.szInfo, sizeof ni.szInfo / sizeof ni.szInfo[0],
             L"check C: and D: free bytes %llu and %llu",
             (unsigned long long)c, (unsigned long long)d);
    added = Shell_NotifyIconW(NIM_ADD, &ni);
    if (added) {
        Sleep(7000);
        Shell_NotifyIconW(NIM_DELETE, &ni);
    }
    DestroyWindow(wnd);
    return added != 0;
}
static int decimal_u32(const wchar_t *s, uint32_t *v)
{
    uint32_t n = 0;
    if (!s || !*s || !v) return 0;
    for (; *s; ++s) {
        uint32_t d;
        if (*s < L'0' || *s > L'9') return 0;
        d = (uint32_t)(*s - L'0');
        if (n > (UINT32_MAX - d) / 10u) return 0;
        n = n * 10u + d;
    }
    *v = n;
    return 1;
}
int wmain(int argc, wchar_t **argv)
{
    dbm_disk_opts policy;
    wchar_t dir[DBM_PATH_CAP], state[DBM_PATH_CAP], events[DBM_PATH_CAP];
    wchar_t env[DBM_PATH_CAP], root[DBM_PATH_CAP];
    HANDLE lock = NULL;
    DWORD wait;
    ULARGE_INTEGER free_c[2], total[2], volume[2];
    int i, rank = 0, prev, notify = 1, have_dir = 0;
    unsigned int have_levels = 0u;
    uint32_t value;
    int result = 2;
    char text[64];
    if (argc == 2 && !wcscmp(argv[1], L"--help")) {
        puts("dbmem_watch [--state-dir PATH] [--warn-percent 15] [--critical-percent 10] [--critical-free-gib 5] [--no-notify]");
        puts("read only C/D disk check. exit 0 OK, 1 arguments, 2 IO, 3 WARN, 4 CRITICAL");
        return 0;
    }
    {
        char *defaults[] = {"dbmem_watch"};
        if (!dbm_disk_parse(1, defaults, &policy)) return 2;
    }
    dir[0] = L'\0';
    for (i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--no-notify") && notify) notify = 0;
        else if (!wcscmp(argv[i], L"--state-dir") && !have_dir && i + 1 < argc) {
            if (wcslen(argv[++i]) >= DBM_PATH_CAP - 32) return 1;
            wcscpy_s(dir, DBM_PATH_CAP, argv[i]);
            have_dir = 1;
        } else if ((!wcscmp(argv[i], L"--warn-percent") ||
                    !wcscmp(argv[i], L"--critical-percent") ||
                    !wcscmp(argv[i], L"--critical-free-gib")) && i + 1 < argc) {
            int which = !wcscmp(argv[i],L"--warn-percent") ? 1 :
                        !wcscmp(argv[i],L"--critical-percent") ? 2 : 3;
            unsigned int flag = 1u << (unsigned int)which;
            if ((have_levels & flag) ||
                !decimal_u32(argv[++i], &value) || value > 1024u) return 1;
            have_levels |= flag;
            if (which == 1) policy.warn_percent = value;
            if (which == 2) policy.critical_percent = value;
            if (which == 3) policy.critical_free_gib = value;
        } else {
            fputs("invalid_options\n", stderr);
            return 1;
        }
    }
    if (policy.warn_percent < 1 || policy.warn_percent > 99 ||
        policy.critical_percent < 1 || policy.critical_percent > 98 ||
        policy.critical_percent >= policy.warn_percent) return 1;
    if (!have_dir) {
        DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", env, DBM_PATH_CAP);
        if (!len || len >= DBM_PATH_CAP - 32) return 2;
        if (swprintf(root, DBM_PATH_CAP, L"%ls\\DBYTEMEM",env) <= 0 ||
            !ensure_dir(root)) return 2;
        if (swprintf(dir, DBM_PATH_CAP,L"%ls\\disk-watch",root) <= 0 ||
            !ensure_dir(dir)) return 2;
    } else if (!ensure_dir(dir)) {
        return 2;
    }
    if (!path_join(state, DBM_PATH_CAP, dir, L"state.txt") ||
        !path_join(events, DBM_PATH_CAP, dir, L"transitions.csv")) return 2;
    lock = CreateMutexW(NULL, FALSE, L"Local\\DBYTEMEM_R6_Disk_Watch");
    if (!lock) return 2;
    wait = WaitForSingleObject(lock, 0);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        CloseHandle(lock);
        return 2;
    }
    for (i = 0; i < 2; ++i) {
        int r;
        if (GetDriveTypeW(names[i]) != DRIVE_FIXED ||
            !GetDiskFreeSpaceExW(names[i], &free_c[i], &total[i], &volume[i])) {
            fprintf(stderr, "disk_unavailable=%s\n", labels[i]);
            goto done;
        }
        r = dbm_disk_rank((uint64_t)free_c[i].QuadPart,
                           (uint64_t)total[i].QuadPart, &policy);
        if (r < 0 || free_c[i].QuadPart > volume[i].QuadPart) goto done;
        if (r == 2) rank = 4;
        else if (r == 1 && rank < 3) rank = 3;
    }
    if (!state_read(state, &prev)) {
        fputs("invalid_prior_state_or_read_error\n", stderr);
        goto done;
    }
    if (prev != rank) {
        if (!event_write(events, prev, rank, (uint64_t)free_c[0].QuadPart,
                          (uint64_t)free_c[1].QuadPart)) goto done;
    }
    {
        int n = snprintf(text,sizeof text,"DBYTEMEM_R6_V1 %s\n",status_name(rank));
        if (n <= 0 || n >= (int)sizeof text ||
            !write_atomic(state,text,(size_t)n)) goto done;
    }
    printf("status=%s previous=%s\n",status_name(rank),
           prev < 0 ? "UNKNOWN" : status_name(prev));
    for (i = 0; i < 2; ++i) {
        printf("drive=%s free_bytes=%llu total_bytes=%llu\n",labels[i],
               (unsigned long long)free_c[i].QuadPart,
               (unsigned long long)total[i].QuadPart);
    }
    fflush(stdout);
    result = rank;
    if (notify && rank > 0 && rank != prev) {
        if (!tray_warn(rank, (uint64_t)free_c[0].QuadPart,
                       (uint64_t)free_c[1].QuadPart))
            fputs("notification=unavailable\n",stderr);
    }
done:
    ReleaseMutex(lock);
    CloseHandle(lock);
    return result;
}
