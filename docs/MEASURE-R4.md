# R4 memory measurement

this is a read-only sampler. it does not change process priority, paging, pagefiles or virtual memory limits.

## commands

```bat
dbmem_series.exe --samples 10 --interval-ms 1000 --phase idle > idle.csv
dbmem_series.exe --samples 10 --interval-ms 1000 --phase load > load.csv
dbmem_series.exe --samples 10 --interval-ms 1000 --phase recovery > recovery.csv
```

the phase label is user-provided. do not label samples idle unless the machine is actually idle. if a load run is requested, the operator runs a separate reproducible workload. this tool does not launch or manage it.

each CSV row has a sample index, phase, actual elapsed milliseconds since start, UTC FILETIME 100 ns ticks since 1601-01-01, total/available physical bytes, Windows reported memory load, page size, commit used/limit in pages, system cache pages, kernel nonpaged pool pages and process count.

the sampling interval is a requested sleep duration. actual elapsed time may be longer because Windows schedules the thread and metric retrieval takes time. sample 1 is taken immediately.

limit: 120 samples at most. intervals 100 to 10000 ms. planned sleep <= 300000 ms. no background service and no automatic startup. output goes to stdout only. redirecting stdout creates a user-owned CSV file.

these metrics do not attribute hard page faults or prove memory latency. system cache and available pages overlap in Windows definitions and must not be summed. high physical occupancy alone is not evidence of a memory manager defect.

## tests

portable parser tests cover missing and duplicate values, overflow, invalid phases and total duration.

Windows gate compiles with MSVC /std:c11 /W4 /WX /O2 and checks 3 CSV samples with monotonically increasing elapsed times, the exact number of columns and rejection of invalid arguments.

CI restrictions on GitHub are documented at https://github.com/Deadbytes101/DBYTEMEM/issues/4

## future acceptance

collect multiple representative idle, workload and recovery runs. compare measured changes without claiming cause from one sample. hard page fault attribution requires an independent ETW-based source with separate validation.
