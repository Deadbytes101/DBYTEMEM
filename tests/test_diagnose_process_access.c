/* DBYTEMEM R11 limited process query contract, Windows x64. CC0-1.0 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define PSAPI_VERSION 1
#include <windows.h>
#include <psapi.h>
#include <stdio.h>

int main(void)
{
    HANDLE p;
    PROCESS_MEMORY_COUNTERS_EX info;
    DWORD pid=GetCurrentProcessId();
    ZeroMemory(&info,sizeof info);
    info.cb=(DWORD)sizeof info;
    p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!p) {
        fprintf(stderr,"limited_open_error=%lu\n",(unsigned long)GetLastError());
        return 1;
    }
    if(!GetProcessMemoryInfo(p,(PROCESS_MEMORY_COUNTERS*)&info,
                             (DWORD)sizeof info)) {
        DWORD err=GetLastError();
        CloseHandle(p);
        fprintf(stderr,"limited_memory_query_error=%lu\n",(unsigned long)err);
        return 2;
    }
    CloseHandle(p);
    if(info.WorkingSetSize==0 || info.PeakWorkingSetSize<info.WorkingSetSize ||
       info.PrivateUsage==0) {
        fputs("limited_memory_query_invariant_failed\n",stderr);
        return 3;
    }
    puts("limited_process_query=PASS");
    return 0;
}
