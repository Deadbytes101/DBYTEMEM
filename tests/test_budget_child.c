/* R9 isolated test child for committed-memory job object semantics. CC0-1.0 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
int wmain(int argc,wchar_t **argv)
{
    BYTE *mem;
    size_t count,i;
    if(argc==4 && !wcscmp(argv[1],L"--check-quoting")) {
        if(wcscmp(argv[2],L"space inside") ||
           wcscmp(argv[3],L"quote\"and\\trail\\")) {wprintf(L"arg2=<%ls> arg3=<%ls> raw=<%ls>\n",argv[2],argv[3],GetCommandLineW());return 74;}
        puts("argument_quoting=PASS");return 0;
    }
    if(argc!=3 || wcscmp(argv[1],L"--alloc-mib")) return 75;
    if(!wcscmp(argv[2],L"80")) count=(size_t)80u*1024u*1024u;
    else if(!wcscmp(argv[2],L"8")) count=(size_t)8u*1024u*1024u;
    else return 75;
    mem=(BYTE*)VirtualAlloc(NULL,count,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!mem){puts("allocation_denied=1");return 73;}
    for(i=0;i<count;i+=4096u)mem[i]=0x5a;
    printf("allocation_succeeded_bytes=%llu\n",(unsigned long long)count);
    if(!VirtualFree(mem,0,MEM_RELEASE))return 76;
    return 0;
}
