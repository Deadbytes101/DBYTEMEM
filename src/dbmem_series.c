/* DBYTEMEM R4 - read-only Windows memory time series. CC0-1.0 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define PSAPI_VERSION 1
#include <windows.h>
#include <psapi.h>
#include <stdint.h>
#include <stdio.h>
#include "series_args.h"

static uint64_t utc_filetime(void)
{
    FILETIME ft;
    ULARGE_INTEGER stamp;
    GetSystemTimeAsFileTime(&ft);
    stamp.LowPart = ft.dwLowDateTime;
    stamp.HighPart = ft.dwHighDateTime;
    return (uint64_t)stamp.QuadPart;
}

static int collect(unsigned int sample, const dbm_series_opts *o,
                   ULONGLONG elapsed_ms)
{
    MEMORYSTATUSEX m;
    PERFORMANCE_INFORMATION p;
    ZeroMemory(&m,sizeof(m));
    ZeroMemory(&p,sizeof(p));
    m.dwLength = (DWORD)sizeof(m);
    if (!GlobalMemoryStatusEx(&m)) {
        fprintf(stderr,"GlobalMemoryStatusEx_error=%lu sample=%u\n",
                GetLastError(),sample);
        return 0;
    }
    if (!GetPerformanceInfo(&p,(DWORD)sizeof(p))) {
        fprintf(stderr,"GetPerformanceInfo_error=%lu sample=%u\n",
                GetLastError(),sample);
        return 0;
    }
    printf("%u,%s,%llu,%llu,%llu,%llu,%lu,%llu,%llu,%llu,%llu,%llu,%lu\n",
           sample, o->phase, (unsigned long long)elapsed_ms,
           (unsigned long long)utc_filetime(),
           (unsigned long long)m.ullTotalPhys,
           (unsigned long long)m.ullAvailPhys,
           (unsigned long)m.dwMemoryLoad,
           (unsigned long long)p.PageSize,
           (unsigned long long)p.CommitTotal,
           (unsigned long long)p.CommitLimit,
           (unsigned long long)p.SystemCache,
           (unsigned long long)p.KernelNonpaged,
           (unsigned long)p.ProcessCount);
    return fflush(stdout) == 0;
}

int main(int argc, char **argv)
{
    dbm_series_opts o;
    ULONGLONG start;
    uint32_t n;
    if (argc == 2 && !strcmp(argv[1],"--help")) {
        puts("usage: dbmem_series.exe [--samples 1..120] [--interval-ms 100..10000] [--phase idle|load|recovery|unclassified]");
        puts("maximum planned duration: 300000 ms; stdout CSV; read-only; no hard fault attribution");
        return 0;
    }
    if (!dbm_series_parse(argc, argv, &o)) {
        fputs("invalid_options\n",stderr);
        return 1;
    }
    puts("sample,phase,elapsed_ms,utc_filetime_100ns,physical_total_bytes,physical_available_bytes,memory_load_percent,page_size_bytes,commit_total_pages,commit_limit_pages,system_cache_pages,kernel_nonpaged_pages,process_count");
    start = GetTickCount64();
    for (n = 0; n < o.samples; ++n) {
        ULONGLONG elapsed = GetTickCount64() - start;
        if (!collect(n + 1u, &o, elapsed)) return 2;
        if (n + 1u != o.samples) {
            Sleep(o.interval_ms);
        }
    }
    return 0;
}
