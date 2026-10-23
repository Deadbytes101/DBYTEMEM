/* DBYTEMEM R10: read-only Win32 memory-pressure and process commit diagnosis. CC0-1.0 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define PSAPI_VERSION 1
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum { MAX_ROWS=8192 };
typedef struct process_row {
    DWORD pid;
    SIZE_T private_bytes;
    SIZE_T working_set_bytes;
    wchar_t name[MAX_PATH];
} process_row;
static int parse_uint(const char *s,unsigned int *out)
{
    unsigned int n=0;
    if(!s||!*s) return 0;
    for(;*s;s++) {
        unsigned int d;
        if(*s<'0'||*s>'9') return 0;
        d=(unsigned int)(*s-'0');
        if(n>(UINT32_MAX-d)/10u) return 0;
        n=n*10u+d;
    }
    *out=n;return 1;
}
static int greater_private(const void *a,const void *b)
{
    const process_row *x=(const process_row*)a,*y=(const process_row*)b;
    if(x->private_bytes>y->private_bytes) return -1;
    if(x->private_bytes<y->private_bytes) return 1;
    return x->pid<y->pid?-1:x->pid>y->pid?1:0;
}
static void print_safe_name(const wchar_t *name)
{
    char utf8[MAX_PATH*4];
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name,-1,
                              utf8,(int)sizeof utf8,NULL,NULL);
    size_t i;
    if(!n){fputs("invalid_name",stdout);return;}
    for(i=0;utf8[i];i++) {
        unsigned char ch=(unsigned char)utf8[i];
        if(ch==',' || ch=='\n'||ch=='\r'||ch=='"'||ch<' ')
            utf8[i]='_';
    }
    fputs(utf8,stdout);
}
static int counter(PDH_HCOUNTER h,double *out)
{
    PDH_FMT_COUNTERVALUE v;
    DWORD type=0;
    ZeroMemory(&v,sizeof v);
    if(PdhGetFormattedCounterValue(h,PDH_FMT_DOUBLE,&type,&v)!=ERROR_SUCCESS ||
       (v.CStatus!=PDH_CSTATUS_VALID_DATA &&
        v.CStatus!=PDH_CSTATUS_NEW_DATA) ||
       !(v.doubleValue>=0.0 && v.doubleValue<1.0e15)) return 0;
    *out=v.doubleValue;return 1;
}
static int processes(process_row *rows,unsigned int *number,
                     unsigned int *denied,unsigned int *truncated,
                     unsigned int *open_access_denied,
                     unsigned int *open_other_failed,
                     unsigned int *query_failed)
{
    HANDLE snapshot;
    PROCESSENTRY32W ent;
    BOOL more;
    *number=0;*denied=0;*truncated=0;
    *open_access_denied=0;*open_other_failed=0;*query_failed=0;
    snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) return 0;
    ZeroMemory(&ent,sizeof ent);
    ent.dwSize=(DWORD)sizeof ent;
    more=Process32FirstW(snapshot,&ent);
    if(!more){CloseHandle(snapshot);return 0;}
    do {
        HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                             FALSE,ent.th32ProcessID);
        if(h) {
            PROCESS_MEMORY_COUNTERS_EX mem;
            ZeroMemory(&mem,sizeof mem);
            mem.cb=(DWORD)sizeof mem;
            if(GetProcessMemoryInfo(h,(PROCESS_MEMORY_COUNTERS*)&mem,
                                    (DWORD)sizeof mem)) {
                if(*number<MAX_ROWS) {
                    process_row *r=&rows[(*number)++];
                    r->pid=ent.th32ProcessID;
                    r->private_bytes=mem.PrivateUsage;
                    r->working_set_bytes=mem.WorkingSetSize;
                    wcsncpy_s(r->name,MAX_PATH,ent.szExeFile,_TRUNCATE);
                } else *truncated=1;
            } else {(*denied)++;(*query_failed)++;}
            CloseHandle(h);
        } else {
            DWORD error=GetLastError();
            (*denied)++;
            if(error==ERROR_ACCESS_DENIED) (*open_access_denied)++;
            else (*open_other_failed)++;
        }
        more=Process32NextW(snapshot,&ent);
    } while(more);
    CloseHandle(snapshot);
    qsort(rows,*number,sizeof *rows,greater_private);
    return 1;
}
static void usage(void)
{
    puts("dbmem_diagnose.exe [--seconds 1..10] [--top 0..20]");
    puts("read-only; page read/write operations include mapped files, not just pagefile");
    puts("process private bytes are not sum of resident RAM; no tuning or process mutation");
}
int main(int argc,char **argv)
{
    unsigned int seconds=1,top=12,seen=0,i,rows_n=0,denied=0,truncated=0;
    unsigned int open_access_denied=0,open_other_failed=0,query_failed=0;
    PDH_HQUERY q=NULL;
    PDH_HCOUNTER reads=NULL,writes=NULL,standby=NULL;
    MEMORYSTATUSEX m;
    PERFORMANCE_INFORMATION p;
    process_row *rows=NULL;
    PDH_STATUS st;
    double read_rate=0,write_rate=0,standby_bytes=0;
    int rc=2;
    if(argc==2 && !strcmp(argv[1],"--help")){usage();return 0;}
    for(i=1;i<(unsigned int)argc;i++) {
        unsigned int v,flag;
        if(!strcmp(argv[i],"--seconds")) flag=1u;
        else if(!strcmp(argv[i],"--top")) flag=2u;
        else {fputs("invalid_options\n",stderr);return 1;}
        if(seen&flag || i+1u>=(unsigned int)argc ||
           !parse_uint(argv[++i],&v)) {
            fputs("invalid_options\n",stderr);return 1;
        }
        seen|=flag;
        if(flag==1u) seconds=v;else top=v;
    }
    if(seconds<1u||seconds>10u||top>20u) {
        fputs("invalid_options\n",stderr);return 1;
    }
    st=PdhOpenQueryW(NULL,0,&q);
    if(st!=ERROR_SUCCESS) {fprintf(stderr,"pdh_open_error=%ld\n",(long)st);goto done;}
    if(PdhAddEnglishCounterW(q,L"\\Memory\\Page Reads/sec",0,&reads)!=ERROR_SUCCESS ||
       PdhAddEnglishCounterW(q,L"\\Memory\\Page Writes/sec",0,&writes)!=ERROR_SUCCESS ||
       PdhAddEnglishCounterW(q,L"\\Memory\\Standby Cache Normal Priority Bytes",0,&standby)!=ERROR_SUCCESS) {
        fputs("pdh_counter_unavailable\n",stderr);goto done;
    }
    if(PdhCollectQueryData(q)!=ERROR_SUCCESS) {
        fputs("pdh_first_sample_failed\n",stderr);goto done;
    }
    Sleep(seconds*1000u);
    if(PdhCollectQueryData(q)!=ERROR_SUCCESS ||
       !counter(reads,&read_rate) ||
       !counter(writes,&write_rate) ||
       !counter(standby,&standby_bytes)) {
        fputs("pdh_second_sample_invalid\n",stderr);goto done;
    }
    ZeroMemory(&m,sizeof m);
    ZeroMemory(&p,sizeof p);
    m.dwLength=(DWORD)sizeof m;
    p.cb=(DWORD)sizeof p;
    if(!GlobalMemoryStatusEx(&m) || !GetPerformanceInfo(&p,sizeof p) ||
       p.CommitTotal>p.CommitLimit || p.PageSize==0) {
        fputs("memory_snapshot_invalid\n",stderr);goto done;
    }
    rows=(process_row*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,
                                  MAX_ROWS*sizeof *rows);
    if(!rows || !processes(rows,&rows_n,&denied,&truncated,
                            &open_access_denied,&open_other_failed,&query_failed)) {
        fputs("process_snapshot_failed\n",stderr);goto done;
    }
    printf("sampling_seconds=%u\n",seconds);
    printf("physical_total_bytes=%llu\n",(unsigned long long)m.ullTotalPhys);
    printf("physical_available_bytes=%llu\n",(unsigned long long)m.ullAvailPhys);
    printf("commit_total_bytes=%llu\n",(unsigned long long)p.CommitTotal*(unsigned long long)p.PageSize);
    printf("commit_limit_bytes=%llu\n",(unsigned long long)p.CommitLimit*(unsigned long long)p.PageSize);
    printf("commit_headroom_bytes=%llu\n",(unsigned long long)(p.CommitLimit-p.CommitTotal)*(unsigned long long)p.PageSize);
    printf("standby_normal_priority_bytes=%.0f\n",standby_bytes);
    printf("kernel_nonpaged_bytes=%llu\n",(unsigned long long)p.KernelNonpaged*(unsigned long long)p.PageSize);
    printf("kernel_paged_bytes=%llu\n",(unsigned long long)p.KernelPaged*(unsigned long long)p.PageSize);
    printf("page_reads_per_sec=%.3f\n",read_rate);
    printf("page_writes_per_sec=%.3f\n",write_rate);
    printf("processes_read=%u\n",rows_n);
    printf("processes_unreadable=%u\n",denied);
    printf("processes_open_access_denied=%u\n",open_access_denied);
    printf("processes_open_other_failed=%u\n",open_other_failed);
    printf("processes_memory_query_failed=%u\n",query_failed);
    printf("processes_truncated=%u\n",truncated);
    puts("process_name,pid,private_commit_bytes,working_set_bytes");
    for(i=0;i<top && i<rows_n;i++) {
        print_safe_name(rows[i].name);
        printf(",%lu,%llu,%llu\n",(unsigned long)rows[i].pid,
               (unsigned long long)rows[i].private_bytes,
               (unsigned long long)rows[i].working_set_bytes);
    }
    if(fflush(stdout))goto done;
    rc=0;
done:
    if(rows)HeapFree(GetProcessHeap(),0,rows);
    if(q)PdhCloseQuery(q);
    return rc;
}
