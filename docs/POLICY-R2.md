# process memory priority

R2 changes the paging priority hint of one selected process. it does not change CPU scheduling priority or give ownership of physical pages. values 1 to 5 are supported. 5 is normal.

## read only

```bat
dbmem_policy.exe inspect --pid 1234
dbmem_policy.exe plan --pid 1234 --to 2
```

plan prints `created_filetime_100ns` and `memory_priority`. copy the exact values into an apply command. do not reuse a stale plan.

## opt-in change

```bat
dbmem_policy.exe apply --pid 1234 --to 2 --expect-created 134000000000000000 --expect-priority 5 --journal C:\Users\Public\dbmem-1234.dbmj --confirm
```

this is an **example only**. replace all values with a fresh plan and use a private writable directory instead of the Public folder on a real machine. the journal path must not already exist and must use a drive-absolute path. `--confirm` is required.

process creation time prevents a recycled PID from being mistaken for the earlier process. expected old priority rejects stale observations. these are checks, not an atomic Windows compare-and-swap.

R2 writes and flushes the journal before setting priority. it holds the journal file exclusively while applying and checking. failures after journal creation leave the journal as evidence. a crash may leave the target modified and requires an explicit restore.

## restore

```bat
dbmem_policy.exe restore --journal C:\Users\Private\dbmem-1234.dbmj --confirm
```

restore opens the recorded PID and verifies its creation time. if Windows reports another value besides the recorded old or requested priority, it refuses to overwrite a third-party decision. if the process no longer exists, no mutation happens. on success the file stays present. deletion is a manual filesystem action.

## scope and failure cases

- inspect/plan need PROCESS_QUERY_LIMITED_INFORMATION
- apply/restore additionally need PROCESS_SET_INFORMATION
- protected or foreign processes can deny OpenProcess
- changing the process default memory priority does not retroactively assign priority to every already-present page
- no registry changes, service, system-wide memory trim, kernel driver or automatic startup
- the journal checksum catches accidental corruption, not an adversary who can edit the journal
- Windows can deny API changes or a process can exit during a request. no global rollback guarantee exists

references: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocessinformation
https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation
https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-memory_priority_information
