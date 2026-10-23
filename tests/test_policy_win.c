/* Win32 integration: real API change to THIS test process only. CC0-1.0 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include "../src/policy_contract.h"

static int run(const wchar_t *exe, const wchar_t *args, int expect_success)
{
    STARTUPINFOW startup;
    PROCESS_INFORMATION pi;
    wchar_t cmd[2048];
    DWORD status;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&pi, sizeof(pi));
    startup.cb = sizeof(startup);
    if (swprintf(cmd, sizeof(cmd)/sizeof(cmd[0]), L"\"%ls\" %ls", exe, args) < 0) return 0;
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &pi)) return 0;
    if (WaitForSingleObject(pi.hProcess, 20000) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(pi.hProcess, &status)) status = 0xFFFFFFFF;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return expect_success ? status == 0 : status != 0 && status != 0xFFFFFFFF;
}

static int current(DWORD *priority, ULONGLONG *created)
{
    MEMORY_PRIORITY_INFORMATION m;
    FILETIME a,b,c,d;
    ULARGE_INTEGER u;
    if (!GetProcessInformation(GetCurrentProcess(), ProcessMemoryPriority,
                               &m, sizeof(m))) return 0;
    if (!GetProcessTimes(GetCurrentProcess(), &a,&b,&c,&d)) return 0;
    u.LowPart=a.dwLowDateTime; u.HighPart=a.dwHighDateTime;
    *priority=m.MemoryPriority;
    *created=u.QuadPart;
    return dbm_priority_valid(*priority) && *created != 0;
}

int wmain(void)
{
    wchar_t exe[MAX_PATH], directory[MAX_PATH], journal[MAX_PATH] = {0}, args[1200];
    wchar_t *slash;
    DWORD n, original, now, target;
    MEMORY_PRIORITY_INFORMATION alternate;
    ULONGLONG creation, post;
    HANDLE f;
    unsigned char byte;
    DWORD written;
    int ok = 0;
    n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) goto done;
    slash = wcsrchr(exe, L'\\');
    if (!slash) goto done;
    wcscpy_s(slash+1, MAX_PATH-(size_t)(slash+1-exe), L"dbmem_policy.exe");
    n = GetTempPathW(MAX_PATH, directory);
    if (!n || n >= MAX_PATH) goto done;
    if (swprintf(journal, MAX_PATH, L"%lsdbmem-r2-%lu-%llu.dbmj", directory,
                 (unsigned long)GetCurrentProcessId(),
                 (unsigned long long)GetTickCount64()) < 0) goto done;
    if (!current(&original, &creation)) goto done;
    target = original == 5 ? 4 : 5;
    if (!run(exe, L"inspect --self", 1)) goto done;
    if (swprintf(args, 1200, L"plan --self --to %lu", (unsigned long)target) < 0 ||
        !run(exe,args,1)) goto done;
    if (swprintf(args,1200,L"apply --pid %lu --to %lu --expect-created %llu --expect-priority %lu --journal \"%ls\" --confirm",
                 (unsigned long)GetCurrentProcessId(), (unsigned long)target,
                 (unsigned long long)(creation+1),(unsigned long)original,journal) < 0 ||
        !run(exe,args,0)) goto done;
    if (GetFileAttributesW(journal) != INVALID_FILE_ATTRIBUTES) goto done;
    if (swprintf(args,1200,L"apply --pid %lu --to %lu --expect-created %llu --expect-priority %lu --journal \"%ls\" --confirm",
                 (unsigned long)GetCurrentProcessId(),(unsigned long)target,
                 (unsigned long long)creation,(unsigned long)original,journal) < 0 ||
        !run(exe,args,1)) goto done;
    if (!current(&now,&post) || now != target || post != creation) goto done;
    /* an existing journal must not be overwritten by a second apply */
    if (swprintf(args,1200,L"apply --pid %lu --to %lu --expect-created %llu --expect-priority %lu --journal \"%ls\" --confirm",
                 (unsigned long)GetCurrentProcessId(),(unsigned long)original,
                 (unsigned long long)creation,(unsigned long)target,journal) < 0 ||
        !run(exe,args,0)) goto done;
    if (!current(&now,&post) || now != target) goto done;
    /* simulate a third-party change on this test process */
    alternate.MemoryPriority = 3;
    if (target == 3 || original == 3) alternate.MemoryPriority = 2;
    if (!SetProcessInformation(GetCurrentProcess(), ProcessMemoryPriority,
                               &alternate, (DWORD)sizeof(alternate))) goto done;
    if (!current(&now,&post) || now != alternate.MemoryPriority) goto done;
    if (swprintf(args,1200,L"restore --journal \"%ls\" --confirm",journal) < 0 ||
        !run(exe,args,0)) goto done;
    if (!current(&now,&post) || now != alternate.MemoryPriority) goto done;
    alternate.MemoryPriority = target;
    if (!SetProcessInformation(GetCurrentProcess(), ProcessMemoryPriority,
                               &alternate, (DWORD)sizeof(alternate))) goto done;
    if (!run(exe,args,1)) goto done;
    if (!current(&now,&post) || now != original || post != creation) goto done;
    if (!run(exe,args,1)) goto done; /* already restored must be idempotent */
    f = CreateFileW(journal,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if (f == INVALID_HANDLE_VALUE) goto done;
    byte = 0;
    if (!WriteFile(f,&byte,1,&written,NULL) || written != 1) {
        CloseHandle(f); goto done;
    }
    CloseHandle(f);
    if (!run(exe,args,0)) goto done; /* corrupted record must be refused */
    if (!current(&now,&post) || now != original) goto done;
    ok = 1;
done:
    if (journal[0]) DeleteFileW(journal); /* test-owned file */
    puts(ok ? "policy_win_integration=PASS" : "policy_win_integration=FAIL");
    return ok ? 0 : 1;
}
