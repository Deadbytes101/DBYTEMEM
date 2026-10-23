/* DBYTEMEM R2 - process memory priority control with recovery. CC0-1.0 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include "policy_contract.h"

typedef struct options {
    DWORD pid;
    DWORD target;
    DWORD expected;
    ULONGLONG created;
    const wchar_t *journal;
    int has_pid, self, has_target, has_expected, has_created, has_journal, confirm;
} options;

static int decimal(const wchar_t *s, uint64_t limit, uint64_t *out)
{
    uint64_t n = 0;
    if (!s || !*s) return 0;
    while (*s) {
        uint32_t digit;
        if (*s < L'0' || *s > L'9') return 0;
        digit = (uint32_t)(*s - L'0');
        if (digit > limit || n > (limit - digit) / 10) return 0;
        n = n * 10 + digit;
        ++s;
    }
    *out = n;
    return 1;
}

static int local_path(const wchar_t *s)
{
    size_t n;
    if (!s) return 0;
    n = wcslen(s);
    if (n < 4 || n >= MAX_PATH) return 0;
    return (((s[0] >= L'A' && s[0] <= L'Z') ||
             (s[0] >= L'a' && s[0] <= L'z')) &&
            s[1] == L':' && (s[2] == L'\\' || s[2] == L'/'));
}

static int get_snapshot(HANDLE h, DWORD *pid, ULONGLONG *created, DWORD *priority)
{
    FILETIME create, exit, kernel, user;
    MEMORY_PRIORITY_INFORMATION info;
    ULARGE_INTEGER stamp;
    if (WaitForSingleObject(h, 0) != WAIT_TIMEOUT) return 0;
    if (!GetProcessTimes(h, &create, &exit, &kernel, &user)) return 0;
    if (!GetProcessInformation(h, ProcessMemoryPriority, &info, (DWORD)sizeof(info))) return 0;
    if (!dbm_priority_valid(info.MemoryPriority)) return 0;
    stamp.LowPart = create.dwLowDateTime;
    stamp.HighPart = create.dwHighDateTime;
    *pid = GetProcessId(h);
    *created = stamp.QuadPart;
    *priority = info.MemoryPriority;
    return *pid != 0 && *created != 0;
}

static int put_priority(HANDLE h, DWORD p)
{
    MEMORY_PRIORITY_INFORMATION info;
    info.MemoryPriority = p;
    return SetProcessInformation(h, ProcessMemoryPriority, &info, (DWORD)sizeof(info)) != 0;
}

static int write_journal(const wchar_t *path, const dbm_journal *j, HANDLE *held)
{
    HANDLE f;
    DWORD wrote = 0;
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    if (!WriteFile(f, j, (DWORD)sizeof(*j), &wrote, NULL) ||
        wrote != sizeof(*j) || !FlushFileBuffers(f)) {
        CloseHandle(f);
        /* leave a bad journal as evidence; no process mutation has happened */
        return 0;
    }
    *held = f; /* keep exclusive while applying to prevent concurrent restore */
    return 1;
}

static int read_journal(const wchar_t *path, dbm_journal *j, HANDLE *held)
{
    HANDLE f;
    LARGE_INTEGER sz;
    DWORD got = 0;
    f = CreateFileW(path, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart != (LONGLONG)sizeof(*j) ||
        !ReadFile(f, j, (DWORD)sizeof(*j), &got, NULL) ||
        got != sizeof(*j) || !dbm_journal_valid(j)) {
        CloseHandle(f);
        return 0;
    }
    *held = f;
    return 1;
}

static int parse_options(int argc, wchar_t **argv, options *o)
{
    int i;
    uint64_t n;
    ZeroMemory(o, sizeof(*o));
    for (i = 2; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--self") && !o->self) o->self = 1;
        else if (!wcscmp(argv[i], L"--pid") && !o->has_pid && i+1 < argc) {
            if (!decimal(argv[++i], UINT32_MAX, &n) || !n) return 0;
            o->pid = (DWORD)n;
            o->has_pid = 1;
        } else if (!wcscmp(argv[i], L"--to") && !o->has_target && i+1 < argc) {
            if (!decimal(argv[++i], 5, &n) || !dbm_priority_valid((uint32_t)n)) return 0;
            o->target = (DWORD)n;
            o->has_target = 1;
        } else if (!wcscmp(argv[i], L"--expect-priority") && !o->has_expected && i+1 < argc) {
            if (!decimal(argv[++i], 5, &n) || !dbm_priority_valid((uint32_t)n)) return 0;
            o->expected = (DWORD)n;
            o->has_expected = 1;
        } else if (!wcscmp(argv[i], L"--expect-created") && !o->has_created && i+1 < argc) {
            if (!decimal(argv[++i], UINT64_MAX, &n) || !n) return 0;
            o->created = n;
            o->has_created = 1;
        } else if (!wcscmp(argv[i], L"--journal") && !o->has_journal && i+1 < argc) {
            o->journal = argv[++i];
            if (!local_path(o->journal)) return 0;
            o->has_journal = 1;
        } else if (!wcscmp(argv[i], L"--confirm") && !o->confirm) o->confirm = 1;
        else return 0;
    }
    if (!wcscmp(argv[1], L"inspect"))
        return (o->has_pid != o->self) && !o->has_target && !o->has_expected &&
               !o->has_created && !o->has_journal && !o->confirm;
    if (!wcscmp(argv[1], L"plan"))
        return (o->has_pid != o->self) && o->has_target && !o->has_expected &&
               !o->has_created && !o->has_journal && !o->confirm;
    if (!wcscmp(argv[1], L"apply"))
        return o->has_pid && !o->self && o->has_target && o->has_expected &&
               o->has_created && o->has_journal && o->confirm;
    if (!wcscmp(argv[1], L"restore"))
        return !o->has_pid && !o->self && !o->has_target && !o->has_expected &&
               !o->has_created && o->has_journal && o->confirm;
    return 0;
}

static void usage(void)
{
    puts("dbmem_policy.exe inspect (--pid N|--self)");
    puts("dbmem_policy.exe plan (--pid N|--self) --to 1..5");
    puts("dbmem_policy.exe apply --pid N --to 1..5 --expect-created FILETIME --expect-priority 1..5 --journal C:\\path\\unique.dbmj --confirm");
    puts("dbmem_policy.exe restore --journal C:\\path\\unique.dbmj --confirm");
}

int wmain(int argc, wchar_t **argv)
{
    options o;
    HANDLE process = NULL, journal = INVALID_HANDLE_VALUE;
    DWORD pid, priority, latest, code;
    ULONGLONG created, current_created;
    dbm_journal record;
    int apply, restore, plan, rc = 2;
    if (argc == 2 && !wcscmp(argv[1], L"--help")) { usage(); return 0; }
    if (argc < 3 || !parse_options(argc, argv, &o)) { usage(); return 1; }
    apply = !wcscmp(argv[1], L"apply");
    restore = !wcscmp(argv[1], L"restore");
    plan = !wcscmp(argv[1], L"plan");
    if (restore) {
        if (!read_journal(o.journal, &record, &journal)) {
            fputs("journal_invalid_or_unavailable\n", stderr);
            return 3;
        }
        o.pid = record.pid;
    } else if (o.self) o.pid = GetCurrentProcessId();

    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE |
                          ((apply || restore) ? PROCESS_SET_INFORMATION : 0),
                          FALSE, o.pid);
    if (!process) { fprintf(stderr,"OpenProcess_error=%lu\n",GetLastError()); goto done; }
    if (!get_snapshot(process, &pid, &created, &priority)) {
        fprintf(stderr,"process_snapshot_error=%lu\n",GetLastError()); goto done;
    }
    if (!apply && !restore) {
        printf("pid=%lu\ncreated_filetime_100ns=%llu\nmemory_priority=%lu\n",
               (unsigned long)pid,(unsigned long long)created,(unsigned long)priority);
        if (plan) {
            printf("requested_priority=%lu\nchange_required=%s\n",
                   (unsigned long)o.target,(priority == o.target) ? "false":"true");
            puts("mutation=none");
        }
        rc = 0;
        goto done;
    }
    if (apply) {
        if (pid != o.pid || created != o.created || priority != o.expected) {
            fputs("stale_identity_or_priority\n",stderr); rc = 4; goto done;
        }
        if (!dbm_journal_make(&record, pid, created, priority, o.target)) {
            fputs("no_change_or_invalid_state\n",stderr); rc = 4; goto done;
        }
        if (!write_journal(o.journal, &record, &journal)) {
            fprintf(stderr,"journal_create_or_flush_failed=%lu\n",GetLastError()); rc = 3; goto done;
        }
        if (!put_priority(process, o.target)) {
            fprintf(stderr,"SetProcessInformation_error=%lu journal_retained=1\n",GetLastError());
            rc = 5; goto done;
        }
        if (!get_snapshot(process, &pid, &current_created, &latest) ||
            pid != record.pid || current_created != record.created_100ns ||
            latest != record.requested) {
            if (put_priority(process, record.previous))
                fputs("verification_failed_rollback_attempted journal_retained=1\n",stderr);
            else
                fputs("verification_failed_rollback_failed journal_retained=1\n",stderr);
            rc = 6; goto done;
        }
        printf("applied=1\npid=%lu\ncreated_filetime_100ns=%llu\nprevious_priority=%lu\nnew_priority=%lu\njournal_retained=1\n",
               (unsigned long)record.pid,(unsigned long long)record.created_100ns,
               (unsigned long)record.previous,(unsigned long)latest);
        rc = 0;
        goto done;
    }
    if (!dbm_restore_allowed(&record, pid, created, priority)) {
        fputs("restore_denied_identity_or_intervening_change\n",stderr); rc = 4; goto done;
    }
    if (priority != record.previous && !put_priority(process, record.previous)) {
        fprintf(stderr,"restore_SetProcessInformation_error=%lu\n",GetLastError()); rc = 5; goto done;
    }
    if (!get_snapshot(process, &pid, &current_created, &latest) ||
        pid != record.pid || current_created != record.created_100ns ||
        latest != record.previous) {
        fputs("restore_postcheck_failed journal_retained=1\n",stderr); rc = 6; goto done;
    }
    printf("restored=1\npid=%lu\ncreated_filetime_100ns=%llu\nmemory_priority=%lu\njournal_retained=1\n",
           (unsigned long)pid,(unsigned long long)created,(unsigned long)latest);
    rc = 0;
done:
    code = GetLastError();
    if (process) CloseHandle(process);
    if (journal != INVALID_HANDLE_VALUE) CloseHandle(journal);
    SetLastError(code);
    return rc;
}
