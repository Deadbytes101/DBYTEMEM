/* DBYTEMEM R0 - read-only Win32 memory probe. CC0-1.0. */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define PSAPI_VERSION 1
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

static int parse_decimal(const char *s, unsigned long *out)
{
    unsigned long n;
    char *end;
    if (!s || !*s || *s < '0' || *s > '9') return 0;
    errno = 0;
    n = strtoul(s, &end, 10);
    if (errno == ERANGE || end == s || *end) return 0;
    *out = n;
    return 1;
}

static void utc(void)
{
    SYSTEMTIME s;
    GetSystemTime(&s);
    printf("utc=%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\n",
           (unsigned)s.wYear, (unsigned)s.wMonth, (unsigned)s.wDay,
           (unsigned)s.wHour, (unsigned)s.wMinute, (unsigned)s.wSecond,
           (unsigned)s.wMilliseconds);
}

static int system_report(void)
{
    MEMORYSTATUSEX m;
    PERFORMANCE_INFORMATION p;
    ZeroMemory(&m, sizeof(m));
    ZeroMemory(&p, sizeof(p));
    m.dwLength = (DWORD)sizeof(m);
    if (!GlobalMemoryStatusEx(&m)) {
        fprintf(stderr, "GlobalMemoryStatusEx_error=%lu\n", GetLastError());
        return 0;
    }
    if (!GetPerformanceInfo(&p, (DWORD)sizeof(p))) {
        fprintf(stderr, "GetPerformanceInfo_error=%lu\n", GetLastError());
        return 0;
    }
    puts("[system]");
    utc();
    printf("physical_total_bytes=%llu\n", (unsigned long long)m.ullTotalPhys);
    printf("physical_available_bytes=%llu\n", (unsigned long long)m.ullAvailPhys);
    printf("memory_load_percent=%lu\n", m.dwMemoryLoad);
    printf("page_size_bytes=%llu\n", (unsigned long long)p.PageSize);
    printf("commit_total_pages=%llu\n", (unsigned long long)p.CommitTotal);
    printf("commit_limit_pages=%llu\n", (unsigned long long)p.CommitLimit);
    printf("commit_peak_pages=%llu\n", (unsigned long long)p.CommitPeak);
    printf("physical_available_pages=%llu\n", (unsigned long long)p.PhysicalAvailable);
    printf("system_cache_pages=%llu\n", (unsigned long long)p.SystemCache);
    printf("kernel_paged_pool_pages=%llu\n", (unsigned long long)p.KernelPaged);
    printf("kernel_nonpaged_pool_pages=%llu\n", (unsigned long long)p.KernelNonpaged);
    printf("process_count=%lu\n", p.ProcessCount);
    printf("thread_count=%lu\n", p.ThreadCount);
    return 1;
}

static int process_report(DWORD pid, DWORD interval)
{
    HANDLE h;
    PROCESS_MEMORY_COUNTERS_EX a, b;
    ULONGLONG start, elapsed;
    DWORD d;
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        fprintf(stderr, "OpenProcess_error=%lu pid=%lu\n", GetLastError(), pid);
        return 0;
    }
    ZeroMemory(&a, sizeof(a));
    ZeroMemory(&b, sizeof(b));
    a.cb = (DWORD)sizeof(a);
    b.cb = (DWORD)sizeof(b);
    if (!GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS *)&a, (DWORD)sizeof(a))) {
        fprintf(stderr, "GetProcessMemoryInfo_first_error=%lu\n", GetLastError());
        CloseHandle(h);
        return 0;
    }
    start = GetTickCount64();
    Sleep(interval);
    if (!GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS *)&b, (DWORD)sizeof(b))) {
        fprintf(stderr, "GetProcessMemoryInfo_second_error=%lu\n", GetLastError());
        CloseHandle(h);
        return 0;
    }
    elapsed = GetTickCount64() - start;
    CloseHandle(h);
    d = b.PageFaultCount - a.PageFaultCount; /* modulo 2^32; soft + hard */
    puts("[process]");
    utc();
    printf("pid=%lu\n", pid);
    printf("elapsed_ms=%llu\n", (unsigned long long)elapsed);
    printf("working_set_bytes=%llu\n", (unsigned long long)b.WorkingSetSize);
    printf("peak_working_set_bytes=%llu\n", (unsigned long long)b.PeakWorkingSetSize);
    printf("private_commit_bytes=%llu\n", (unsigned long long)b.PrivateUsage);
    printf("page_fault_count_start=%lu\n", a.PageFaultCount);
    printf("page_fault_count_end=%lu\n", b.PageFaultCount);
    printf("page_fault_count_delta_soft_plus_hard=%lu\n", d);
    return 1;
}

int main(int argc, char **argv)
{
    unsigned long pid = 0, interval = 1000;
    int have_pid = 0, have_interval = 0, i;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--pid") && !have_pid && i + 1 < argc) {
            if (!parse_decimal(argv[++i], &pid) || !pid) goto usage;
            have_pid = 1;
        } else if (!strcmp(argv[i], "--interval-ms") && !have_interval && i + 1 < argc) {
            if (!parse_decimal(argv[++i], &interval) || interval < 100 || interval > 60000) goto usage;
            have_interval = 1;
        } else if (!strcmp(argv[i], "--help") && argc == 2) {
            puts("usage: dbmem.exe [--pid PID [--interval-ms 100..60000]]");
            return 0;
        } else goto usage;
    }
    if (have_interval && !have_pid) goto usage;
    if (!system_report()) return 2;
    if (have_pid && !process_report((DWORD)pid, (DWORD)interval)) return 3;
    return 0;
usage:
    fputs("usage: dbmem.exe [--pid PID [--interval-ms 100..60000]]\n", stderr);
    return 1;
}
