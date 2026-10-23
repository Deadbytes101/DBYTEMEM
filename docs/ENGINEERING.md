# engineering record - R0/R1

baseline: initial GitHub repository contains only README and CC0 license, commit `2b28bfe843c8a7f4cc7f62e31482c1c5c1e4f5bf`

source: R0 probe was drafted in `DBMEM_R0_SOURCE.zip`. reorganized without trusting its earlier unverified Windows build claim. fixes: reject `--interval-ms` without `--pid`, reject repeated parameters, keep page-fault semantics explicit.

R1 contracts:

- allocations are within the caller-owned arena; size zero, bad alignment, overflow and exhaustion fail without cursor mutation
- power-of-two alignment is relative to the actual pointer, not only an offset
- `high_water` is a maximum since initialization and does not decrease on rewind
- Win32 region starts reserved only; commit grows in whole pages and is done before publishing a new allocation cursor
- failed commit leaves `used` and `committed` unchanged
- `reset` decommits pages; `destroy` releases the entire reservation
- no arbitrary free, no concurrency, no pointer validity after rewind/reset/release

verification:

```
gcc -std=c11 -Wall -Wextra -Werror -pedantic -O2 tests/test_arena.c -o arena_test && ./arena_test
clang -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_arena.c -o arena_san && ./arena_san
```

Windows runner must build `src/dbmem.c`, run portable tests, run `tests/test_vm_win.c`, verify `VirtualQuery` state transitions, and exercise CLI invalid-argument rejection.

not proven by this work: actual host Windows build until CI finishes, ETW hard-fault attribution, benchmark improvements, NUMA placement, physical RAM ownership, kernel safety, process policy tuning.

next boundary: R2 should add an **opt-in** process-policy inspector first, record original policy, then guard any mutation behind explicit PID identity/owner, preview and rollback. do not implement global working-set trim or pagefile disabling.

API references:

- https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc
- https://learn.microsoft.com/en-us/windows/win32/memory/freeing-virtual-memory
- https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getperformanceinfo
- https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo
