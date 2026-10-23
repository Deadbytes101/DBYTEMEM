# R5 disk watch

native Win32. one-shot. no service, persistent agent or automatic cleanup.

## sample

```bat
dbmem_disk.exe --drive C --drive D
dbmem_disk.exe --csv
dbmem_disk.exe --drive C --warn-percent 20 --critical-percent 10 --critical-free-gib 5 --csv
```

with no `--drive` the program checks all mounted fixed drive letters returned by GetLogicalDrives and GetDriveTypeW. repeat `--drive` for multiple drives. this does not enumerate mounted folders without a drive letter, network shares or removable drives.

`GetDiskFreeSpaceExW` returns bytes available to the calling security context. `total_bytes_for_caller` can differ from the physical volume total under quotas. `free_bytes_for_caller` can be smaller than `volume_free_bytes`. all counters are bytes. `free_percent` uses caller-visible free bytes divided by caller-visible total and is display-only, rounded to two decimal places. status classification uses integer arithmetic on raw bytes, not the rounded display value.

## classification

defaults:
- WARN when free is strictly below 15% of caller-visible total
- CRITICAL when free is strictly below 10% of caller-visible total or below 5 GiB
- OK otherwise

critical takes precedence over warning. `--warn-percent` accepts 1..99, `--critical-percent` accepts 1..98 and must be less than warning. `--critical-free-gib` accepts 0..1024. 0 disables the fixed size threshold.

the parser rejects unknown, duplicate, negative, non-decimal and overflowing options. percentage comparisons split division and remainder to avoid 64-bit multiplication overflow. percent thresholds use strict less-than on exact integer bytes.

## exit status

| exit | meaning |
| --- | --- |
| 0 | all checked drives OK |
| 1 | invalid CLI arguments |
| 2 | drive absent, not fixed, Windows API failure, invalid counters, or stdout failure |
| 3 | at least one WARN and no CRITICAL |
| 4 | at least one CRITICAL |

the exit status is not a Windows notification. scripts can inspect exit 3/4, but this binary creates no scheduled task and no watch daemon. a failed read is never silently interpreted as OK. the CSV contains no disk content or file paths beyond drive letters.

## evidence

portable tests in `tests/test_disk_policy.c` cover parser boundaries, duplicate and invalid switches, exact percentage boundaries, 5 GiB threshold, malformed counter combinations and UINT64_MAX arithmetic. the native verifier checks the real Win32 counter contract, output columns and negative arguments.

no folders are recursively scanned. no files are deleted or moved. no pagefile, WSL VHD, Registry, disk quotas, memory policies, Windows services or user process priorities are changed.

see Microsoft Learn for GetDiskFreeSpaceExW and GetLogicalDrives. R5 is a disk capacity probe, not a file recovery or performance optimizer.
