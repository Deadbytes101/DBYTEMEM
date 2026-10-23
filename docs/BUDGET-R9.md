# R9 native commit budget

DBYTEMEM can control committed virtual memory of a newly launched process tree through a Windows Job Object. it does not replace the Windows memory manager or manage physical pages.

## behavior

the tool enforces JOB_OBJECT_LIMIT_JOB_MEMORY on the job and JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE. this is a cap on the sum of committed virtual memory of the job, not RAM residency. a program whose commit needs exceed the budget may fail an allocation or terminate. do not use this on programs holding unsaved work.

it creates the child suspended, assigns it to a new job, resumes and waits for its main process to end. when the launcher exits, any child descendants still alive in the job are terminated. nested jobs or breakaway behavior can limit compatibility with some programs.

## CLI

dry-run:

    dbmem_budget.exe plan --limit-mib 512 --exe C:\path\program.exe -- --version

run:

    dbmem_budget.exe run --limit-mib 512 --exe C:\path\program.exe --confirm -- --version

the program path must be a drive-absolute existing executable. program arguments follow the -- separator. the limit is 64..262144 MiB. confirm is required for actual launch.

exit 0 means dry-run or child success. exit 1 means bad input. exit 2 means Windows API failure. exit 3 means nonzero child exit, including allocation denial. printed job limit is committed bytes. no background task or standing rights are granted.

## native regression

the verifier uses an isolated test executable. it tests plan-only, actual 256 MiB budget allowing an 80 MiB allocation, a 64 MiB budget rejecting the same allocation, Windows quoting with spaces/quotes/trailing backslash, missing confirmation and malformed options. it does not touch existing processes.

## rollback and boundaries

closing the last job handle releases the job limit, and terminates remaining job processes by design. there is no persistent system policy to restore. never launch a long-lived app with unsaved data casually under this scope.

no registry, pagefile, memory compression, Windows service, system-wide trim, kernel patch or already-running PID modification is performed. job constraints can control only workloads deliberately launched under them. speed improvements must be measured per workload, not claimed from an artificial memory cap.

Microsoft references:
https://learn.microsoft.com/windows/win32/procthread/job-objects
https://learn.microsoft.com/windows/win32/api/winnt/ns-winnt-jobobject_extended_limit_information
https://learn.microsoft.com/windows/win32/api/jobapi2/nf-jobapi2-assignprocesstojobobject
