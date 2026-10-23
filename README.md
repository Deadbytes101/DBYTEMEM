# DBYTEMEM

native Windows memory inspection and bounded allocator experiments

C and Win32. no frameworks. no kernel patching. no memory cleaners.

this project measures what Windows actually reports and builds memory allocation primitives that are owned by the caller. it does not replace the Windows memory manager.

## build

use the Visual Studio 2022 x64 Native Tools Command Prompt

```bat
cl /nologo /std:c11 /W4 /WX /O2 /TC src\dbmem.c /Fe:dbmem.exe /link psapi.lib
cl /nologo /std:c11 /W4 /WX /O2 /TC tests\test_arena.c /Fe:arena_test.exe
cl /nologo /std:c11 /W4 /WX /O2 /TC tests\test_vm_win.c /Fe:arena_win_test.exe
cl /nologo /std:c11 /W4 /WX /O2 /TC src\dbmem_policy.c /Fe:dbmem_policy.exe
cl /nologo /std:c11 /W4 /WX /O2 /TC src\dbmem_series.c /Fe:dbmem_series.exe /link psapi.lib
cl /nologo /std:c11 /W4 /WX /O2 /TC src\dbmem_disk.c /Fe:dbmem_disk.exe
cl /nologo /std:c11 /W4 /WX /O2 /TC src\dbmem_watch.c /Fe:dbmem_watch.exe /link shell32.lib user32.lib
cl /nologo /std:c11 /W4 /WX /O2 /TC src\dbmem_pressure.c /Fe:dbmem_pressure.exe /link psapi.lib
cl /nologo /std:c11 /W4 /WX /O2 /TC src\dbmem_pressure_watch.c /Fe:dbmem_pressure_watch.exe /link psapi.lib shell32.lib user32.lib
cl /nologo /std:c11 /W4 /WX /O2 /TC tests\test_pressure_policy.c /Fe:pressure_policy_test.exe
cl /nologo /std:c11 /W4 /WX /O2 /TC tests\test_policy_contract.c /Fe:policy_contract_test.exe
cl /nologo /std:c11 /W4 /WX /O2 /TC tests\test_series_args.c /Fe:series_args_test.exe
cl /nologo /std:c11 /W4 /WX /O2 /TC tests\test_disk_policy.c /Fe:disk_policy_test.exe
cl /nologo /std:c11 /W4 /WX /O2 /TC tests\test_policy_win.c /Fe:policy_win_test.exe
arena_test.exe
arena_win_test.exe
policy_contract_test.exe
policy_win_test.exe
dbmem.exe
```

```bat
dbmem.exe --pid 1234 --interval-ms 1000
```

R0 is a read-only probe. values are printed as `key=value` for easy comparison and capture. calls can fail for inaccessible or terminated processes. exit 1 means invalid arguments, 2 system API failed, 3 process API failed.

R1 provides `src/arena.h` for caller-owned bounded arenas and `src/arena_win.h` for Win32 virtual address reservation with on-demand page commitment. no allocation happens in another process. no arbitrary free is offered. a rewind only changes the arena cursor; old pointers become invalid for the next allocation. `dbm_vm_reset` decommits committed pages and invalidates all prior pointers.

R1 is **experimental**. it is not a drop-in replacement for `malloc` and does not promise higher performance. callers must serialize access; neither arena is thread-safe.

## process memory policy - R2

`dbmem_policy.exe` reads memory priority without changing the target by default

```bat
dbmem_policy.exe inspect --pid 1234
dbmem_policy.exe plan --pid 1234 --to 2
```

R2 also contains opt-in apply and restore. both require explicit `--confirm`. apply pins PID plus creation FILETIME and existing priority from a fresh plan. it flushes a unique recovery journal before calling `SetProcessInformation`. restore refuses PID reuse and unexpected third-party changes. the journal remains on disk for inspection even after restore. see `docs/POLICY-R2.md` for exact commands and limits.

## native verification and R3 benchmark

run `scripts/verify-native-windows.ps1` from a Visual Studio x64 developer prompt to build and test all native binaries. the script writes SHA-256 hashes and test results into `build-msvc/NATIVE-VERIFY.txt`.

`bench/bench_alloc_win.c` measures a fixed batch-lifetime workload against Windows Heap. it never modifies another process. different free semantics mean the measured ratio is not a general speed claim. see `docs/VERIFICATION-R3.md`.

## R4 memory time series

`dbmem_series.exe` records a bounded read-only series of Windows memory counters to stdout as CSV. the phase label is supplied by the operator and is not detected by Windows.

```bat
dbmem_series.exe --samples 10 --interval-ms 1000 --phase idle > idle.csv
dbmem_series.exe --samples 10 --interval-ms 1000 --phase load > load.csv
dbmem_series.exe --samples 10 --interval-ms 1000 --phase recovery > recovery.csv
```

samples are 1..120. interval is 100..10000 milliseconds. planned delay cannot exceed 300000 milliseconds. each row has a real elapsed clock and a UTC FILETIME timestamp. the program does not start the workload itself and cannot distinguish hard from soft page faults. see `docs/MEASURE-R4.md`.

## R5 disk watch

`dbmem_disk.exe` reads free capacity for fixed drive letters. it does not delete files or change disk settings.

```bat
dbmem_disk.exe --drive C --drive D
dbmem_disk.exe --csv
```

by default warn below 15% available. report critical below 10% or below 5 GiB free. limits are operator-settable and use strict comparison. exit 0 means OK, 3 means WARN, 4 means CRITICAL, 1 means invalid options and 2 means read error. these codes are useful for an external scheduler but this tool installs no schedule or service. see `docs/DISK-R5.md` for API and metric semantics.

## R6 native disk recovery watch

`dbmem_watch.exe` checks fixed C: and D: drive letters, keeps a bounded local transition journal and attempts a tray warning only when a WARN or CRITICAL state begins. its output never authorizes cleanup. the Windows Task Scheduler setup is opt-in and uses the normal logged-on user without elevation. the verifier checks OK, WARN, repeat WARN, CRITICAL, recovery and invalid state on the native executable. see `docs/DISK-R6.md` for the setup and rollback commands.

## R7 memory pressure inspection

`dbmem_pressure.exe` classifies physical available memory and system-wide commit headroom using 3 Win32 snapshots. the median of the 3 severity ranks is returned as exit 0 OK, 3 WARN or 4 CRITICAL; invalid input is 1 and API failure is 2. this is read-only and not an automatic RAM cleaner. see `docs/PRESSURE-R7.md` for thresholds, unit semantics and limitations.

## R8 memory pressure watch

`dbmem_pressure_watch.exe` samples R7 memory pressure and records the current status and 100 recent transitions in one atomic local file. it attempts a Windows tray balloon on new WARN or CRITICAL states and never purges RAM. see `docs/PRESSURE-WATCH-R8.md` for usage, native tests, consent boundary and optional Scheduled Task rollback.

## metrics

- physical available includes memory Windows can reclaim; used RAM is not a synonym for performance loss
- `*_pages` values are counts of pages; multiply by `page_size_bytes` to calculate bytes
- commit is charged virtual memory, not resident memory or pagefile bytes transferred
- `PageFaultCount` includes soft and hard faults; it is not a disk-read counter
- the process sample measures one PID over the supplied interval; system values are a snapshot

## safety

no required elevation, driver, DLL injection, memory purge, registry writes, service changes, or automatic tuning. R0/R1 do not change other processes. R2 changes only one explicitly selected process when the operator supplies the full guarded apply command; recovery is manual and keeps a journal. Windows is still responsible for virtual memory accounting and physical page assignment.

## verification boundary

CI builds on Windows x64 and tests Win32 reserve/commit/decommit states using `VirtualQuery`. Linux tests cover the portable arena and sanitizer checks. a successful CI run is not a proof of performance or reliability on the target user's Windows machine. see `docs/ENGINEERING.md`.

## license

CC0 1.0 Universal. see `LICENSE`.
