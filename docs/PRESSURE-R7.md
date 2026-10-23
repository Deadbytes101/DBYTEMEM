# R7 system memory pressure probe

native C and Win32. no driver. no scheduler. no kernel hooks. no automatic memory purge.

R7 reads two independent system-wide constraints:

- physical available bytes from GlobalMemoryStatusEx. it includes the free, zero and standby page lists. unused RAM alone is not proof of a performance problem.
- remaining system commit from GetPerformanceInfo CommitLimit minus CommitTotal. these are pages, not bytes. the commit limit is a soft limit when pagefiles can grow.

the CLI does not treat MEMORYSTATUSEX ullAvailPageFile as the system-wide commit headroom. it does not call SetProcessWorkingSetSize, EmptyWorkingSet, VirtualLock, or modify other processes.

## thresholds

defaults:

physical available below 15 percent => WARN
physical available below 7 percent => CRITICAL
system commit headroom below 10 percent => WARN
system commit headroom below 3 percent => CRITICAL

the worst of the two dimensions wins. equality is not below the threshold. all threshold checks use integer arithmetic without multiplying a 64-bit quantity by 100.

three observations are taken with a default 250-ms gap. report the median of the three severity ranks, so one outlier alone cannot change the decision. the numeric memory metrics in stdout are from the LAST observation, which may differ from the reported median rank. this is a short local snapshot, not a sustained pressure diagnosis or proof of paging I/O.

## CLI

    dbmem_pressure.exe
    dbmem_pressure.exe --interval-ms 100
    dbmem_pressure.exe --warn-physical-percent 20 --critical-physical-percent 10
    dbmem_pressure.exe --warn-commit-headroom-percent 12 --critical-commit-headroom-percent 4

exit codes: 0 OK; 1 invalid options; 2 failed API/snapshot/output; 3 WARN; 4 CRITICAL.

stdout has deterministic key/value names, an absolute UTC FILETIME, sample-level severities, total and available physical bytes, total/limit/free commit pages, memory load percentage and inter-sample gap. it does not contain a per-process blame ranking.

## build and verify

from a Visual Studio x64 development shell:

    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-native-windows.ps1

this compiles with MSVC C11 /W4 /WX /O2, runs the portable policy test and invokes the actual Win32 executable. forced threshold tests drive WARN and CRITICAL without allocating or exhausting RAM. they are classification tests, not tests under real RAM pressure.

the Linux CI workflow also compiles the portable policy regression with GCC and Clang sanitizers. GitHub Actions is currently blocked for the account and has zero hosted runs; local Windows build results must not be relabeled CI PASS.

## invariants and next stage

R7 is intentionally one-shot, read-only and side-effect-free except for stdout/stderr. It has no automatic memory pressure task, notifications, logs, durable journal or mitigation authority. disk monitoring from R6 remains separate, and the existing production task and its binary must not be replaced by this experimental R7 executable.

R8 can build durable state transitions and opted-in user-level scheduling after standalone R7 validation. no claim of "complete Windows memory control" is made.

official data semantics:
https://learn.microsoft.com/windows/win32/api/sysinfoapi/ns-sysinfoapi-memorystatusex
https://learn.microsoft.com/windows/win32/api/psapi/ns-psapi-performance_information
