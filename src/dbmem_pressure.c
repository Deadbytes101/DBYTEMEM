/* DBYTEMEM R7 read-only memory pressure snapshot. CC0-1.0 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define PSAPI_VERSION 1
#include <windows.h>
#include <psapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pressure_policy.h"

static const char *level_name(int n)
{
    return n == 2 ? "CRITICAL" : n == 1 ? "WARN" : "OK";
}
static int number(const char *s, uint32_t *value)
{
    uint32_t n=0u;
    if(!s || !*s || !value) return 0;
    for (;*s;s++) {
        uint32_t d;
        if (*s<'0' || *s>'9') return 0;
        d=(uint32_t)(*s-'0');
        if(n>(UINT32_MAX-d)/10u) return 0;
        n=n*10u+d;
    }
    *value=n;
    return 1;
}
static int read_sample(dbm_pressure_sample *s, DWORD *memory_load)
{
    MEMORYSTATUSEX m;
    PERFORMANCE_INFORMATION p;
    ZeroMemory(&m,sizeof m);
    ZeroMemory(&p,sizeof p);
    m.dwLength=(DWORD)sizeof m;
    p.cb=(DWORD)sizeof p;
    if(!GlobalMemoryStatusEx(&m)) {
        fprintf(stderr,"GlobalMemoryStatusEx_error=%lu\n",GetLastError());
        return 0;
    }
    if(!GetPerformanceInfo(&p,(DWORD)sizeof p)) {
        fprintf(stderr,"GetPerformanceInfo_error=%lu\n",GetLastError());
        return 0;
    }
    s->physical_total_bytes=(uint64_t)m.ullTotalPhys;
    s->physical_available_bytes=(uint64_t)m.ullAvailPhys;
    s->commit_total_pages=(uint64_t)p.CommitTotal;
    s->commit_limit_pages=(uint64_t)p.CommitLimit;
    *memory_load=m.dwMemoryLoad;
    return 1;
}
int main(int argc,char **argv)
{
    dbm_pressure_policy policy=dbm_pressure_defaults();
    dbm_pressure_sample s;
    DWORD load=0;
    uint32_t delay_ms=250u;
    unsigned int seen=0u;
    int severity[3], i, selected;
    for(i=1;i<argc;i++) {
        unsigned int slot=0u, flag;
        uint32_t val;
        if(argc==2 && !strcmp(argv[i],"--help")) {
            puts("dbmem_pressure [--interval-ms 100..1000] [--warn-physical-percent 1..99] [--critical-physical-percent 1..98] [--warn-commit-headroom-percent 1..99] [--critical-commit-headroom-percent 1..98]");
            puts("3 samples, median severity; physical and system commit headroom. no tuning.");
            puts("exit 0 OK, 1 invalid arguments, 2 API/snapshot error, 3 WARN, 4 CRITICAL");
            return 0;
        }
        if(!strcmp(argv[i],"--interval-ms")) slot=0u;
        else if(!strcmp(argv[i],"--warn-physical-percent")) slot=1u;
        else if(!strcmp(argv[i],"--critical-physical-percent")) slot=2u;
        else if(!strcmp(argv[i],"--warn-commit-headroom-percent")) slot=3u;
        else if(!strcmp(argv[i],"--critical-commit-headroom-percent")) slot=4u;
        else goto invalid;
        flag=1u << slot;
        if((seen&flag) || i+1>=argc || !number(argv[++i],&val))
            goto invalid;
        seen|=flag;
        if(slot==0u) delay_ms=val;
        else if(slot==1u) policy.warn_physical_pct=val;
        else if(slot==2u) policy.critical_physical_pct=val;
        else if(slot==3u) policy.warn_commit_headroom_pct=val;
        else policy.critical_commit_headroom_pct=val;
    }
    if(delay_ms<100u || delay_ms>1000u) goto invalid;
    if(policy.warn_physical_pct<1u || policy.warn_physical_pct>99u ||
       policy.critical_physical_pct<1u ||
       policy.critical_physical_pct>=policy.warn_physical_pct ||
       policy.warn_commit_headroom_pct<1u ||
       policy.warn_commit_headroom_pct>99u ||
       policy.critical_commit_headroom_pct<1u ||
       policy.critical_commit_headroom_pct>=policy.warn_commit_headroom_pct)
       goto invalid;
    for(i=0;i<3;i++) {
        if(!read_sample(&s,&load)) return 2;
        severity[i]=dbm_pressure_rank(&policy,&s);
        if(severity[i]<0) {
            fputs("invalid_system_memory_snapshot\n",stderr);
            return 2;
        }
        if(i!=2) Sleep(delay_ms);
    }
    selected=dbm_pressure_median(severity[0],severity[1],severity[2]);
    if(selected<0) return 2;
    {
        FILETIME ft;
        ULARGE_INTEGER now;
        GetSystemTimeAsFileTime(&ft);
        now.LowPart=ft.dwLowDateTime;
        now.HighPart=ft.dwHighDateTime;
        printf("utc_filetime_100ns=%llu\n",(unsigned long long)now.QuadPart);
    }
    printf("status=%s\n",level_name(selected));
    printf("sample_levels=%s,%s,%s\n",
           level_name(severity[0]),level_name(severity[1]),level_name(severity[2]));
    printf("physical_total_bytes=%llu\n",(unsigned long long)s.physical_total_bytes);
    printf("physical_available_bytes=%llu\n",(unsigned long long)s.physical_available_bytes);
    printf("commit_total_pages=%llu\n",(unsigned long long)s.commit_total_pages);
    printf("commit_limit_pages=%llu\n",(unsigned long long)s.commit_limit_pages);
    printf("commit_headroom_pages=%llu\n",
           (unsigned long long)(s.commit_limit_pages-s.commit_total_pages));
    printf("memory_load_percent=%lu\n",(unsigned long)load);
    printf("sample_interval_ms=%u\n",delay_ms);
    if(fflush(stdout)!=0) return 2;
    return selected==2?4:selected==1?3:0;
invalid:
    fputs("invalid_options\n",stderr);
    return 1;
}
