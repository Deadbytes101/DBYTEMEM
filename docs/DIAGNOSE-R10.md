# R10 Windows memory diagnosis

R10 is a read-only native Win32 and PDH profiler for determining whether Windows is actually short of physical RAM, system commit, or hard paging I/O.

run:

    dbmem_diagnose.exe --seconds 1 --top 12
    dbmem_diagnose.exe --seconds 5 --top 20

--seconds accepts 1..10. --top accepts 0..20 and 0 suppresses process rows. invalid input exits 1, observation/API failure exits 2, successful observation exits 0.

## measurements

- physical_available_bytes includes memory Windows can reclaim, including standby. a large standby list is normally useful filesystem cache.
- commit_total_bytes and commit_limit_bytes are global committed virtual-memory accounting, not bytes in pagefile.sys.
- standby_normal_priority_bytes is the PDH normal-priority standby subset. do not add it to physical available; these metrics overlap.
- page_reads_per_sec and page_writes_per_sec are PDH Memory counters from two samples at least one second apart. hard page reads can come from EXE/DLL, memory-mapped files, or pagefile, and are not attributed to any particular PID.
- kernel_nonpaged_bytes and kernel_paged_bytes are system pools based on GetPerformanceInfo page counts.
- process_name, pid, private_commit_bytes, working_set_bytes are a best-effort Toolhelp + GetProcessMemoryInfo snapshot, sorted by private commit. individual processes of a program appear as separate rows. names and PIDs can change during collection, and the top list is not a causal proof of slowdown.
- processes_unreadable counts processes that cannot be opened or read; inaccessible system processes may not appear in the ranking. processes_truncated means the bounded list was too small, so the ranking must not be treated as exhaustive.
- the tool prints no process arguments, executable paths, usernames, document paths, logs or network data.

## diagnosis and optimization policy

Only consider tuning when the user sees a reproducible slowdown and the simultaneous sample establishes a corresponding bottleneck. Repeat idle/load/recovery, capture 3 or more samples, and compare hard paging with I/O latency and commit headroom before acting. Exposing a cap with R9 is not proof that the cap will make programs faster.

On the measured Windows 11 PC (23 October 2026), approximately 15 GiB of available physical memory and near-zero page-read/write operations do NOT justify disabling the system pagefile, WSL, QEMU, memory compression, changing registry policies, or purging standby cache. WSL Debian has active development state, so leave it running.

R10 makes no memory policy modifications. it does not install a service, scheduled task, driver, registry setting or profiler resident process. closing it has nothing to roll back.

## source API

Windows PDH:
https://learn.microsoft.com/windows/win32/api/pdh/nf-pdh-pdhaddenglishcounterw
https://learn.microsoft.com/windows/win32/perfctrs/collecting-performance-data

Win32:
https://learn.microsoft.com/windows/win32/api/tlhelp32/nf-tlhelp32-process32firstw
https://learn.microsoft.com/windows/win32/psapi/process-memory-usage-information

Microsoft guidance:
https://learn.microsoft.com/troubleshoot/windows-client/performance/how-to-determine-the-appropriate-page-file-size-for-64-bit-versions-of-windows
