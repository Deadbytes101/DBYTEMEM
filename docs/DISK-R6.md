# R6 native disk watch

native C. one shot. no service, memory cleaner, driver or scheduled task is installed by the build.

the program checks C: and D: using GetDiskFreeSpaceExW. it reuses the R5 disk policy. classification uses caller-visible available space, not raw disk clusters.

    dbmem_watch.exe --help
    dbmem_watch.exe --no-notify
    dbmem_watch.exe --warn-percent 20 --critical-percent 10 --critical-free-gib 5

defaults are WARN below 15% free and CRITICAL below 10% or 5 GiB free. both drives must be readable or the whole check fails with exit 2.

## exit and evidence

- 0: OK
- 1: invalid command line
- 2: Windows API, disk, state or journal error
- 3: WARN
- 4: CRITICAL

no disk file contents are read. nothing is deleted. no pagefile or Windows memory policies are changed.

under %LOCALAPPDATA%\DBYTEMEM\disk-watch the program creates:

- state.txt: single versioned current status
- transitions.csv: up to 100 previous changes in status

the CSV has no header. each row is UTC FILETIME, previous status, current status, C available bytes, D available bytes.

same status on repeated calls does not append a transition. invalid state refuses modification. native writes use same-directory temporary files, a flushed C stream and MoveFileExW with replace and write-through. these are independent writes, not a two-file database transaction: an interrupted write between journal and state can create a repeated transition on the next run. no silent success is reported for a failed write.

a local mutex prevents overlapping runs. an opt-in Windows tray balloon is attempted only when the current state enters WARN or CRITICAL. the shell may suppress it due to notification settings, quiet time or a missing interactive desktop. stdout and the transition journal are stronger evidence than a balloon. use --no-notify to disable UI for testing.

## verify

from the Visual Studio x64 developer command prompt:

    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-native-windows.ps1

the existing tracked verification script builds native C, then checks OK -> WARN -> repeat WARN -> CRITICAL -> recovery without filling the drives. it checks the four transition rows, corrupt state denial and invalid CLI refusal.

the MSVC build is equivalent to:

    cl /nologo /std:c11 /W4 /WX /O2 /TC src\dbmem_watch.c /Fe:dbmem_watch.exe /link shell32.lib user32.lib

the GCC Windows build also uses -municode and links shell32 and user32.

## opt-in scheduled task

the executable is intentionally a one-shot check. run on a schedule only after the native verifier passes. install the verified exe into a fixed path and register a normal-user interactive Scheduled Task. this does not require an administrator token. use a regular Windows PowerShell console in the integrated main checkout.

    $bin = Join-Path $env:LOCALAPPDATA 'DBYTEMEM\bin'
    New-Item -ItemType Directory -Force -Path $bin | Out-Null
    $source = (Resolve-Path 'build-msvc\dbmem_watch.exe').Path
    $target = Join-Path $bin 'dbmem_watch.exe'
    if (Test-Path $target) { throw 'existing watcher; inspect before replacement' }
    Copy-Item -LiteralPath $source -Destination $target
    if ((Get-FileHash $source -Algorithm SHA256).Hash -ne (Get-FileHash $target -Algorithm SHA256).Hash) { throw 'copy mismatch' }
    if (Get-ScheduledTask -TaskName 'DBYTEMEM Disk Watch' -ErrorAction SilentlyContinue) { throw 'task already exists' }
    $a = New-ScheduledTaskAction -Execute $target
    $t = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(2) -RepetitionInterval (New-TimeSpan -Minutes 30) -RepetitionDuration (New-TimeSpan -Days 3650)
    $p = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
    $s = New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 2) -StartWhenAvailable
    Register-ScheduledTask -TaskName 'DBYTEMEM Disk Watch' -Action $a -Trigger $t -Principal $p -Settings $s

inspect and undo:

    Get-ScheduledTask -TaskName 'DBYTEMEM Disk Watch'
    Get-ScheduledTaskInfo -TaskName 'DBYTEMEM Disk Watch'
    Unregister-ScheduledTask -TaskName 'DBYTEMEM Disk Watch' -Confirm:$false

unregister stops future checks. the installed executable and transition evidence remain intact. the program runs only in the user's interactive session. if the user is logged off the task cannot deliver a tray notification. Task Scheduler may show a nonzero LastTaskResult for WARN or CRITICAL by design.

upgrades are explicit: stop or unregister the task, verify the new binary and copy it only after reviewing the old file. do not overwrite unrelated tasks, binaries or user data.

## release boundary

GitHub Actions is disabled for this GitHub user. the native-ci dispatch returned HTTP 422 even though the workflow is active and repository Actions settings report enabled. there are zero hosted runs. issue #4 tracks this account restriction. native local PASS does not mean hosted CI PASS.
