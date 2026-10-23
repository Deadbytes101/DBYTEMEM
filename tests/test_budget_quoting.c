/* Native test drives R9 argv directly, bypassing PowerShell legacy native quote loss. CC0-1.0 */
#define wmain dbmem_budget_main
#include "../src/dbmem_budget.c"
#undef wmain
#include <wchar.h>
int wmain(int argc,wchar_t **argv)
{
    wchar_t *args[11];
    if(argc!=2) return 91;
    args[0]=L"dbmem_budget.exe";
    args[1]=L"run";
    args[2]=L"--limit-mib";
    args[3]=L"256";
    args[4]=L"--exe";
    args[5]=argv[1];
    args[6]=L"--confirm";
    args[7]=L"--";
    args[8]=L"--check-quoting";
    args[9]=L"space inside";
    args[10]=L"quote\"and\\trail\\";
    return dbmem_budget_main(11,args);
}
