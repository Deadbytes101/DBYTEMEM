/* DBYTEMEM R9: explicitly launched child job memory budget. CC0-1.0 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include <limits.h>

enum { CMD_MAX=32767, PATH_MAX_LOCAL=32768 };
static int decimal(const wchar_t *s,uint32_t *value)
{
    uint32_t n=0;
    if(!s||!*s||!value) return 0;
    for(;*s;s++) {
        uint32_t d;
        if(*s<L'0'||*s>L'9') return 0;
        d=(uint32_t)(*s-L'0');
        if(n>(UINT32_MAX-d)/10u) return 0;
        n=n*10u+d;
    }
    *value=n; return 1;
}
static int absolute_exe(const wchar_t *src,wchar_t *out)
{
    DWORD got,attr;
    if(!src || wcslen(src)<4u || !( ((src[0]>=L'A'&&src[0]<=L'Z') ||
                   (src[0]>=L'a'&&src[0]<=L'z')) &&
                  src[1]==L':' && (src[2]==L'\\'||src[2]==L'/'))) return 0;
    got=GetFullPathNameW(src,PATH_MAX_LOCAL,out,NULL);
    if(!got||got>=PATH_MAX_LOCAL) return 0;
    attr=GetFileAttributesW(out);
    if(attr==INVALID_FILE_ATTRIBUTES || (attr&FILE_ATTRIBUTE_DIRECTORY))
        return 0;
    return 1;
}
static int append(wchar_t *cmd,size_t *used,wchar_t ch)
{
    if(*used>=CMD_MAX-1) return 0;
    cmd[(*used)++]=ch;cmd[*used]=L'\0';return 1;
}
/* Windows CRT argument quoting: double backslashes before a quote/end quote. */
static int quoted(wchar_t *cmd,size_t *used,const wchar_t *arg)
{
    const wchar_t *s=arg;
    if(!append(cmd,used,L'"')) return 0;
    while(*s) {
        unsigned int slash=0,j;
        while(*s==L'\\'){slash++;s++;}
        if(*s==L'"') {
            if(slash>100000u) return 0;
            for(j=0;j<slash*2u+1u;j++)if(!append(cmd,used,L'\\'))return 0;
            if(!append(cmd,used,L'"')) return 0;
            s++;
        } else if(*s==L'\0') {
            if(slash>100000u) return 0;
            for(j=0;j<slash*2u;j++)if(!append(cmd,used,L'\\'))return 0;
        } else {
            for(j=0;j<slash;j++)if(!append(cmd,used,L'\\'))return 0;
            if(!append(cmd,used,*s++))return 0;
        }
    }
    return append(cmd,used,L'"');
}
static void usage(void)
{
    puts("dbmem_budget.exe plan --limit-mib 64..262144 --exe C:\\path\\app.exe [-- args...]");
    puts("dbmem_budget.exe run --limit-mib 64..262144 --exe C:\\path\\app.exe --confirm [-- args...]");
    puts("job aggregate committed memory budget; child tree killed when launcher exits");
    puts("only explicitly launched children, not running Windows processes or RAM usage");
}
int wmain(int argc,wchar_t **argv)
{
    wchar_t *cmd=NULL,*path=NULL;
    int isrun=0,hadlimit=0,hadexe=0,confirmed=0,endopts=0,argindex=-1,i,rc=2;
    uint32_t mib=0;
    const wchar_t *exe=NULL;
    HANDLE job=NULL;
    PROCESS_INFORMATION pi;
    STARTUPINFOW si;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit;
    DWORD exitcode=0,err=0,wait;
    size_t used=0;
    ZeroMemory(&pi,sizeof pi);
    if(argc==2 && !wcscmp(argv[1],L"--help")){usage();return 0;}
    if(argc<6){usage();return 1;}
    if(!wcscmp(argv[1],L"run")) isrun=1;
    else if(wcscmp(argv[1],L"plan")) {usage();return 1;}
    for(i=2;i<argc;i++){
        if(!wcscmp(argv[i],L"--")){endopts=1;argindex=i+1;break;}
        if(!wcscmp(argv[i],L"--confirm") && isrun && !confirmed){confirmed=1;continue;}
        if(!wcscmp(argv[i],L"--limit-mib") && !hadlimit && i+1<argc){
            if(!decimal(argv[++i],&mib) || mib<64u || mib>262144u)goto invalid;
            hadlimit=1;continue;
        }
        if(!wcscmp(argv[i],L"--exe") && !hadexe && i+1<argc){
            exe=argv[++i];hadexe=1;continue;
        }
        goto invalid;
    }
    if(!hadlimit||!hadexe||(!endopts&&argindex!=-1)||
       (isrun&&!confirmed)||(!isrun&&confirmed))goto invalid;
    path=(wchar_t*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,
                            PATH_MAX_LOCAL*sizeof(wchar_t));
    cmd=(wchar_t*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,
                           CMD_MAX*sizeof(wchar_t));
    if(!path||!cmd){fputs("allocation_failed\n",stderr);goto done;}
    if(!absolute_exe(exe,path)){fputs("invalid_or_missing_absolute_exe\n",stderr);rc=1;goto done;}
    if(!quoted(cmd,&used,path))goto invalid;
    if(endopts) {
        for(i=argindex;i<argc;i++) {
            if(!append(cmd,&used,L' ') || !quoted(cmd,&used,argv[i]))goto invalid;
        }
    }
    wprintf(L"exe=%ls\n",path);
    wprintf(L"job_commit_limit_mib=%u\n",mib);
    wprintf(L"job_commit_limit_bytes=%llu\n",(unsigned long long)mib*1024ull*1024ull);
    if(!isrun){wprintf(L"mutation=none\n");rc=0;goto done;}
    ZeroMemory(&si,sizeof si);ZeroMemory(&pi,sizeof pi);
    ZeroMemory(&limit,sizeof limit);si.cb=sizeof si;
    limit.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_JOB_MEMORY|JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    limit.JobMemoryLimit=(SIZE_T)mib*1024u*1024u;
    job=CreateJobObjectW(NULL,NULL);
    if(!job){err=GetLastError();goto winerr;}
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,
                                &limit,sizeof limit)){err=GetLastError();goto winerr;}
    if(!CreateProcessW(path,cmd,NULL,NULL,FALSE,CREATE_SUSPENDED,
                       NULL,NULL,&si,&pi)){err=GetLastError();goto winerr;}
    if(!AssignProcessToJobObject(job,pi.hProcess)){
        err=GetLastError();
        TerminateProcess(pi.hProcess,1);
        WaitForSingleObject(pi.hProcess,5000);
        goto winerr;
    }
    if(ResumeThread(pi.hThread)==(DWORD)-1){
        err=GetLastError();
        TerminateJobObject(job,1);
        WaitForSingleObject(pi.hProcess,5000);
        goto winerr;
    }
    wprintf(L"launched_pid=%lu\n",(unsigned long)pi.dwProcessId);
    fflush(stdout);
    wait=WaitForSingleObject(pi.hProcess,INFINITE);
    if(wait!=WAIT_OBJECT_0 || !GetExitCodeProcess(pi.hProcess,&exitcode)) {
        err=GetLastError();goto winerr;
    }
    wprintf(L"child_exit_code=%lu\n",(unsigned long)exitcode);
    wprintf(L"job_closed=1\n");
    rc=exitcode==0?0:3;goto done;
winerr:
    fprintf(stderr,"win32_error=%lu\n",(unsigned long)err);
    rc=2;goto done;
invalid:
    fputs("invalid_options\n",stderr);rc=1;
done:
    if(pi.hThread) CloseHandle(pi.hThread);
    if(pi.hProcess) CloseHandle(pi.hProcess);
    if(job) CloseHandle(job);
    if(cmd) HeapFree(GetProcessHeap(),0,cmd);
    if(path) HeapFree(GetProcessHeap(),0,path);
    return rc;
}
