# R11 minimal process access

R10's process enumeration requested both PROCESS_QUERY_LIMITED_INFORMATION and PROCESS_VM_READ for every PID. on Windows 11 the GetProcessMemoryInfo API only requires PROCESS_QUERY_LIMITED_INFORMATION (or PROCESS_QUERY_INFORMATION). requiring VM_READ imposes unnecessary access checks and can hide process memory counters from ordinary, non-elevated users.

R11 removes PROCESS_VM_READ. it does not request SeDebugPrivilege, bypass protected processes, use undocumented structures or modify another process.

the read-only CLI remains:

    dbmem_diagnose.exe --seconds 5 --top 12

extra key/value fields:

- processes_open_access_denied
- processes_open_other_failed
- processes_memory_query_failed

these sum to processes_unreadable. a process may exit between Toolhelp and OpenProcess, and Windows may still deny access to selected protected processes. a complete ranking is not guaranteed. processes_read is how many memory structures were obtained, not how many running PIDs exist globally. do not infer source of leaks from an incomplete result.

native verification includes a separate test executable proving that OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION) can return process memory statistics without VM_READ, plus the existing diagnostic live probe, a breakdown invariant and all historical regression gates.

the user's initial non-elevated R10 result on 23 October 2026 was processes_read=137 and processes_unreadable=177. a first elevated R11 smoke had processes_read=316 and unreadable=1. these are different security contexts and times, not a controlled comparison. a non-elevated R11 rerun is needed to establish whether this resolves the user's 177 unreadable observation.

system changes: none. no memory compression tweak, standby purge, pagefile edit, Job Object applied to live user apps, WSL shutdown or scheduled task.

source:
https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo
https://learn.microsoft.com/en-us/windows/win32/procthread/process-security-and-access-rights
