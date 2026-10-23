/* DBYTEMEM R8 native user-scoped memory pressure watch. CC0-1.0 */
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define PSAPI_VERSION 1
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <limits.h>
#include <io.h>
#include "pressure_policy.h"

enum { PATH_CAP=768, LINE_CAP=160, MAX_EVENTS=100 };
static const char *rank_name(int r) { return r==2?"CRITICAL":r==1?"WARN":"OK"; }
static int getrank(const char *s)
{
    if(!strcmp(s,"OK")) return 0;
    if(!strcmp(s,"WARN")) return 1;
    if(!strcmp(s,"CRITICAL")) return 2;
    if(!strcmp(s,"UNKNOWN")) return -1;
    return -2;
}
static int number(const wchar_t *s, uint32_t *out)
{
    uint32_t n=0;
    if(!s || !*s || !out) return 0;
    for(;*s;s++) {
        uint32_t d;
        if(*s<L'0'||*s>L'9') return 0;
        d=(uint32_t)(*s-L'0');
        if(n>(UINT32_MAX-d)/10u) return 0;
        n=n*10u+d;
    }
    *out=n;
    return 1;
}
static int sample(dbm_pressure_sample *s)
{
    MEMORYSTATUSEX m;
    PERFORMANCE_INFORMATION p;
    ZeroMemory(&m,sizeof m);
    ZeroMemory(&p,sizeof p);
    m.dwLength=(DWORD)sizeof m;
    p.cb=(DWORD)sizeof p;
    if(!GlobalMemoryStatusEx(&m) ||
       !GetPerformanceInfo(&p,(DWORD)sizeof p)) return 0;
    s->physical_total_bytes=(uint64_t)m.ullTotalPhys;
    s->physical_available_bytes=(uint64_t)m.ullAvailPhys;
    s->commit_total_pages=(uint64_t)p.CommitTotal;
    s->commit_limit_pages=(uint64_t)p.CommitLimit;
    return 1;
}
static int mkdir_owned(const wchar_t *dir)
{
    DWORD attr;
    if(CreateDirectoryW(dir,NULL)) return 1;
    if(GetLastError()!=ERROR_ALREADY_EXISTS) return 0;
    attr=GetFileAttributesW(dir);
    return attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_DIRECTORY);
}
static int pathjoin(wchar_t *dest,const wchar_t *dir,const wchar_t *part)
{
    int n=swprintf(dest,PATH_CAP,L"%ls\\%ls",dir,part);
    return n>0 && n<PATH_CAP;
}
static int atomic_replace(const wchar_t *path,const char *bytes,size_t count)
{
    wchar_t temp[PATH_CAP];
    FILE *f=NULL;
    int n=swprintf(temp,PATH_CAP,L"%ls.tmp.%lu",path,
                   (unsigned long)GetCurrentProcessId());
    if(n<=0 || n>=PATH_CAP) return 0;
    if(_wfopen_s(&f,temp,L"wb")!=0 || !f) return 0;
    if(fwrite(bytes,1,count,f)!=count || fflush(f) ||
       _commit(_fileno(f))) { fclose(f); DeleteFileW(temp); return 0; }
    if(fclose(f)) { DeleteFileW(temp); return 0; }
    if(!MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp); return 0;
    }
    return 1;
}
static int complete_line(const char *text)
{
    size_t n=strlen(text);
    return n>0 && text[n-1]=='\n';
}
/* strict exact three-field event parser; no malformed legacy state is trusted */
static int decimal_token(const char **text, char separator)
{
    const char *p=*text;
    unsigned long long n=0;
    if(*p<'0' || *p>'9') return 0;
    do {
        unsigned int d=(unsigned int)(*p-'0');
        if(n>(ULLONG_MAX-d)/10u) return 0;
        n=n*10u+d;
        ++p;
    } while(*p>='0' && *p<='9');
    if(*p!=separator) return 0;
    *text=p+1;
    return 1;
}
static int snapshot_valid(const char *line)
{
    const char *p=line;
    unsigned int i;
    if(strncmp(line,"SNAPSHOT ",9)) return 0;
    p+=9;
    for(i=0;i<5;i++) {
        if(!decimal_token(&p,i==4?'\n':' ')) return 0;
    }
    return *p=='\0';
}
static int event_valid(const char *line)
{
    unsigned long long stamp;
    char from[16],to[16];
    const char *p=line;
    int end=0;
    if(strncmp(line,"EVENT ",6)) return 0;
    p+=6;
    if(!decimal_token(&p,' ')) return 0;
    if(sscanf(line,"EVENT %llu %15s %15s%n",&stamp,from,to,&end)!=3 ||
       end<=0 || line[end]!='\n' || line[end+1]!='\0') return 0;
    return (getrank(from)>=-1 && getrank(to)>=0 && stamp>0);
}
static int read_journal(const wchar_t *file,int *previous,
                        char lines[MAX_EVENTS][LINE_CAP],unsigned int *count)
{
    FILE *f=NULL;
    char head[LINE_CAP],snapshot[LINE_CAP];
    unsigned int used=0;
    int end=0;
    unsigned long long stamp,total,free_phys,commit,limit;
    errno_t err=_wfopen_s(&f,file,L"rb");
    *previous=-1;
    *count=0;
    if(err==ENOENT) return 1;
    if(err!=0 || !f) return 0;
    if(!fgets(head,sizeof head,f) || !complete_line(head) ||
       !fgets(snapshot,sizeof snapshot,f) || !complete_line(snapshot)) goto bad;
    if(!strcmp(head,"DBMEM_PRESSURE_WATCH_V1 OK\n")) *previous=0;
    else if(!strcmp(head,"DBMEM_PRESSURE_WATCH_V1 WARN\n")) *previous=1;
    else if(!strcmp(head,"DBMEM_PRESSURE_WATCH_V1 CRITICAL\n")) *previous=2;
    else goto bad;
    if(!snapshot_valid(snapshot) ||
       sscanf(snapshot,"SNAPSHOT %llu %llu %llu %llu %llu%n",
              &stamp,&total,&free_phys,&commit,&limit,&end)!=5 ||
       end<=0 || snapshot[end]!='\n' || snapshot[end+1]!='\0' ||
       !stamp || !total || free_phys>total || !limit || commit>limit) goto bad;
    for(;;) {
        char line[LINE_CAP];
        if(!fgets(line,sizeof line,f)) {
            if(ferror(f)) goto bad;
            break;
        }
        if(!complete_line(line) || !event_valid(line) ||
           used==MAX_EVENTS) goto bad;
        memcpy(lines[used++],line,strlen(line)+1);
    }
    if(fclose(f)) return 0;
    *count=used;
    return 1;
bad:
    fclose(f);
    return 0;
}
static int tray_warn(int rank)
{
    NOTIFYICONDATAW ni;
    HWND hwnd=CreateWindowExW(0,L"STATIC",L"DBYTEMEM",WS_OVERLAPPED,
                              0,0,0,0,NULL,NULL,GetModuleHandleW(NULL),NULL);
    BOOL added;
    if(!hwnd) return 0;
    ZeroMemory(&ni,sizeof ni);
    ni.cbSize=sizeof ni;
    ni.hWnd=hwnd;
    ni.uID=1;
    ni.uFlags=NIF_ICON|NIF_TIP|NIF_INFO;
    ni.hIcon=LoadIconW(NULL,MAKEINTRESOURCEW(32515));
    ni.dwInfoFlags=NIIF_WARNING|NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(ni.szTip,128,L"DBYTEMEM memory watch");
    wcscpy_s(ni.szInfoTitle,64,
              rank==2?L"DBYTEMEM RAM CRITICAL":L"DBYTEMEM RAM WARN");
    wcscpy_s(ni.szInfo,256,
              L"System memory pressure changed. check DBYTEMEM journal.");
    added=Shell_NotifyIconW(NIM_ADD,&ni);
    if(added) {
        Sleep(5000);
        Shell_NotifyIconW(NIM_DELETE,&ni);
    }
    DestroyWindow(hwnd);
    return added!=0;
}
int wmain(int argc,wchar_t **argv)
{
    wchar_t dir[PATH_CAP],root[PATH_CAP],file[PATH_CAP],env[PATH_CAP];
    dbm_pressure_policy policy=dbm_pressure_defaults();
    dbm_pressure_sample s;
    int ranks[3],current,prev,notify=1,owned=0,code=2,i;
    unsigned int seen=0,n=0;
    uint32_t delay=250;
    char events[MAX_EVENTS][LINE_CAP];
    char record[LINE_CAP*(MAX_EVENTS+3)];
    size_t pos=0;
    HANDLE mutex=NULL;
    DWORD wait;
    FILETIME ft;
    ULARGE_INTEGER stamp;
    if(argc==2 && !wcscmp(argv[1],L"--help")) {
        puts("dbmem_pressure_watch [--state-dir PATH] [--interval-ms 100..1000] [--warn-physical-percent N] [--critical-physical-percent N] [--warn-commit-headroom-percent N] [--critical-commit-headroom-percent N] [--no-notify]");
        puts("local read-only RAM/commit probe with atomic bounded journal");
        puts("exit 0 OK; 1 invalid; 2 IO/API; 3 WARN; 4 CRITICAL");
        return 0;
    }
    dir[0]=L'\0';
    for(i=1;i<argc;i++) {
        unsigned int slot=0,flag;
        uint32_t val;
        if(!wcscmp(argv[i],L"--no-notify")) {
            if(!notify) return 1;
            notify=0; continue;
        }
        if(!wcscmp(argv[i],L"--state-dir")) {
            if(owned || i+1>=argc || wcslen(argv[++i])>=PATH_CAP-40) return 1;
            wcscpy_s(dir,PATH_CAP,argv[i]); owned=1; continue;
        }
        if(!wcscmp(argv[i],L"--interval-ms")) slot=0;
        else if(!wcscmp(argv[i],L"--warn-physical-percent")) slot=1;
        else if(!wcscmp(argv[i],L"--critical-physical-percent")) slot=2;
        else if(!wcscmp(argv[i],L"--warn-commit-headroom-percent")) slot=3;
        else if(!wcscmp(argv[i],L"--critical-commit-headroom-percent")) slot=4;
        else return 1;
        flag=1u<<slot;
        if((seen&flag) || i+1>=argc || !number(argv[++i],&val)) return 1;
        seen|=flag;
        if(slot==0) delay=val;
        else if(slot==1) policy.warn_physical_pct=val;
        else if(slot==2) policy.critical_physical_pct=val;
        else if(slot==3) policy.warn_commit_headroom_pct=val;
        else policy.critical_commit_headroom_pct=val;
    }
    if(delay<100 || delay>1000 ||
       policy.warn_physical_pct<1 || policy.warn_physical_pct>99 ||
       policy.critical_physical_pct<1 ||
       policy.critical_physical_pct>=policy.warn_physical_pct ||
       policy.warn_commit_headroom_pct<1 ||
       policy.warn_commit_headroom_pct>99 ||
       policy.critical_commit_headroom_pct<1 ||
       policy.critical_commit_headroom_pct>=policy.warn_commit_headroom_pct) return 1;
    if(!owned) {
        DWORD len=GetEnvironmentVariableW(L"LOCALAPPDATA",env,PATH_CAP);
        if(!len || len>=PATH_CAP-40 ||
           swprintf(root,PATH_CAP,L"%ls\\DBYTEMEM",env)<=0 ||
           !mkdir_owned(root) ||
           swprintf(dir,PATH_CAP,L"%ls\\pressure-watch",root)<=0)
           return 2;
    }
    if(!mkdir_owned(dir) || !pathjoin(file,dir,L"state.txt")) return 2;
    mutex=CreateMutexW(NULL,FALSE,L"Local\\DBYTEMEM_PressureWatch_R8");
    if(!mutex) return 2;
    wait=WaitForSingleObject(mutex,0);
    if(wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED) {
        CloseHandle(mutex); return 2;
    }
    for(i=0;i<3;i++) {
        if(!sample(&s)) goto done;
        ranks[i]=dbm_pressure_rank(&policy,&s);
        if(ranks[i]<0) goto done;
        if(i<2) Sleep(delay);
    }
    current=dbm_pressure_median(ranks[0],ranks[1],ranks[2]);
    if(current<0 || !read_journal(file,&prev,events,&n)) goto done;
    GetSystemTimeAsFileTime(&ft);
    stamp.LowPart=ft.dwLowDateTime;
    stamp.HighPart=ft.dwHighDateTime;
    {
        int k=snprintf(record,sizeof record,
          "DBMEM_PRESSURE_WATCH_V1 %s\nSNAPSHOT %llu %llu %llu %llu %llu\n",
          rank_name(current),(unsigned long long)stamp.QuadPart,
          (unsigned long long)s.physical_total_bytes,
          (unsigned long long)s.physical_available_bytes,
          (unsigned long long)s.commit_total_pages,
          (unsigned long long)s.commit_limit_pages);
        if(k<=0 || (size_t)k>=sizeof record) goto done;
        pos=(size_t)k;
    }
    if(prev!=current) {
        if(n==MAX_EVENTS) {
            memmove(events,events+1,(MAX_EVENTS-1u)*LINE_CAP);
            n=MAX_EVENTS-1u;
        }
        {
            int k=snprintf(events[n],LINE_CAP,"EVENT %llu %s %s\n",
                (unsigned long long)stamp.QuadPart,
                prev<0?"UNKNOWN":rank_name(prev),rank_name(current));
            if(k<=0 || k>=LINE_CAP) goto done;
            n++;
        }
    }
    for(i=0;i<(int)n;i++) {
        size_t len=strlen(events[i]);
        if(len>=sizeof record-pos) goto done;
        memcpy(record+pos,events[i],len);
        pos+=len;
    }
    if(!atomic_replace(file,record,pos)) goto done;
    printf("status=%s\n",rank_name(current));
    printf("previous=%s\n",prev<0?"UNKNOWN":rank_name(prev));
    printf("physical_available_bytes=%llu\n",
           (unsigned long long)s.physical_available_bytes);
    printf("commit_headroom_pages=%llu\n",
           (unsigned long long)(s.commit_limit_pages-s.commit_total_pages));
    printf("events=%u\n",n);
    fflush(stdout);
    code=current==2?4:current==1?3:0;
    if(notify && current>0 && current!=prev && !tray_warn(current))
        fputs("notification=unavailable\n",stderr);
done:
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return code;
}
