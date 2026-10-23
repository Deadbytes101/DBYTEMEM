# R3 measurement and native gate

R2 base commit 73545e58c8a19831b3948b90cf89a9c905899558

the Windows x64 host ran both GCC 16.1.0 and MSVC 19.51 with warnings as errors.
R0 snapshot, R1 arena / VirtualQuery, R2 journal contract and apply / restore passed on Windows.
R2 tests modify only their own short-lived test process and restore its memory priority.

the R2 integration test now covers three more failure cases

- an existing journal cannot be overwritten
- restore refuses an intervening priority change
- restoration succeeds after the test process returns to the journal's requested value

the R3 benchmark exercises 2048 allocations of 64 bytes over 128 batches.
it runs five repeats per strategy and alternates measurement order.
maximum live payload is 131072 bytes.
the arena reserves virtual memory then commits pages on demand.
the heap allocates and frees each object individually.
the arena decommits the entire batch.

this is not a general allocator comparison.
the arena cannot free individual objects.
results describe only this batch-lifetime workload.

observed on Windows x64 October 23 2026

| compiler | Heap median ns per block | arena VM median ns per block |
| --- | ---: | ---: |
| MSVC 19.51 | 76.076 | 26.083 |
| GCC 16.1.0 | 76.215 | 26.493 |

do not use one timing sample to claim universal performance.
do not change Windows global memory policy based on this result.

from a Visual Studio x64 developer prompt

    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-native-windows.ps1

the script builds seven native test binaries and writes build-msvc/NATIVE-VERIFY.txt.
this receipt is not a signed attestation.
