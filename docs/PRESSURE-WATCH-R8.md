# R8 native scheduled memory pressure watch

one-shot Win32 executable with bounded durable state. no memory cleaner, pagefile changes, driver or process mutation.

R8 classifies physical available memory and system-wide commit headroom using three observations and the R7 portable policy. default separation is 250 ms. a single local status file records the current severity, last metrics and up to 100 changes.

## usage

    dbmem_pressure_watch.exe
    dbmem_pressure_watch.exe --no-notify --interval-ms 100
    dbmem_pressure_watch.exe --no-notify --state-dir D:\test-owned-folder

if state-dir is omitted, use %LOCALAPPDATA%\DBYTEMEM\pressure-watch.

exit 0 OK; 1 invalid options; 2 API, filesystem, journal or concurrency error; 3 WARN; 4 CRITICAL.

defaults: physical available below 15% WARN and 7% CRITICAL. system commit headroom below 10% WARN and 3% CRITICAL. any one dimension can raise severity. CLI can override each threshold. thresholds change classification only and do not use extra RAM.

## journal

the file state.txt has a versioned first line, last-sample metrics and up to 100 transitions:

    DBMEM_PRESSURE_WATCH_V1 OK
    SNAPSHOT <filetime> <physical_total> <physical_available> <commit_total_pages> <commit_limit_pages>
    EVENT <filetime> <from> <to>

one atomic rename publishes both the new status and bounded transition history together. a temporary file is flushed with _commit before MoveFileExW replace/write-through. failed writes retain the old journal file. this does not guarantee survival of all recent writes on power loss or storage failure.

the program rejects malformed records rather than rewriting them, and holds a local named mutex to prevent concurrent writes. repeated severity does not append events. the newest numerical metrics are from the last of three observations, while the displayed classification is their median rank.

## notification

on a new WARN or CRITICAL transition R8 attempts a Windows tray balloon. this is best effort: Windows may suppress notifications. --no-notify disables UI. recovery to OK is recorded without a popup. no remote notification or automatic corrective action is implemented.

## testing

run scripts\verify-native-windows.ps1 from a Visual Studio x64 developer shell. R8 acceptance exercises OK -> WARN -> repeated WARN -> CRITICAL -> OK, four transitions, corrupted-state denial with hash parity, invalid arguments, and more than 100 transitions with a bounded 100-row ring. tests use their own build directory and simulated threshold changes. they never exhaust system RAM.

## opt-in scheduled task and rollback

after the integrated native verifier passes, copy the verified binary to %LOCALAPPDATA%\DBYTEMEM\bin with SHA-256 parity, and register a limited interactive-user Task Scheduler job named DBYTEMEM Memory Watch with a 30-minute recurrence.

run only while the installing user has an interactive logon session. do not register an elevated task or system service. the existing DBYTEMEM Disk Watch task is independent.

inspect or revoke:

    Get-ScheduledTask -TaskName 'DBYTEMEM Memory Watch'
    Get-ScheduledTaskInfo -TaskName 'DBYTEMEM Memory Watch'
    Unregister-ScheduledTask -TaskName 'DBYTEMEM Memory Watch' -Confirm:$false

unregistering stops future checks and leaves the binary and journal intact for inspection.

GitHub Actions is still blocked at account level with zero hosted runs. native/WSL testing is not GitHub CI PASS. no release tag without separate release readiness proof.
