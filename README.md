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
arena_test.exe
arena_win_test.exe
dbmem.exe
```

```bat
dbmem.exe --pid 1234 --interval-ms 1000
```

R0 is a read-only probe. values are printed as `key=value` for easy comparison and capture. calls can fail for inaccessible or terminated processes. exit 1 means invalid arguments, 2 system API failed, 3 process API failed.

R1 provides `src/arena.h` for caller-owned bounded arenas and `src/arena_win.h` for Win32 virtual address reservation with on-demand page commitment. no allocation happens in another process. no arbitrary free is offered. a rewind only changes the arena cursor; old pointers become invalid for the next allocation. `dbm_vm_reset` decommits committed pages and invalidates all prior pointers.

R1 is **experimental**. it is not a drop-in replacement for `malloc` and does not promise higher performance. callers must serialize access; neither arena is thread-safe.

## metrics

- physical available includes memory Windows can reclaim; used RAM is not a synonym for performance loss
- `*_pages` values are counts of pages; multiply by `page_size_bytes` to calculate bytes
- commit is charged virtual memory, not resident memory or pagefile bytes transferred
- `PageFaultCount` includes soft and hard faults; it is not a disk-read counter
- the process sample measures one PID over the supplied interval; system values are a snapshot

## safety

no elevated privileges, driver, DLL injection, memory purge, registry writes, service changes, automatic tuning, or remote process changes. no persistent system changes. Windows is still responsible for virtual memory accounting and physical page assignment.

## verification boundary

CI builds on Windows x64 and tests Win32 reserve/commit/decommit states using `VirtualQuery`. Linux tests cover the portable arena and sanitizer checks. a successful CI run is not a proof of performance or reliability on the target user's Windows machine. see `docs/ENGINEERING.md`.

## license

CC0 1.0 Universal. see `LICENSE`.
