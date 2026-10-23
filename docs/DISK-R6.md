# R6 scheduled disk recovery watch

one-shot Windows disk check with a bounded local state record. the probe stays read-only.

R6 wraps the R5 native C binary. it does not delete files, purge caches, change Windows Memory Manager, modify the pagefile or install a kernel driver. it is not a filesystem analyzer.

## one run

build the native R5 probe with the MSVC verifier, then:

    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\disk-watch-r6.ps1 -NoNotify

the script checks C: and D: and writes two files to:

    %LOCALAPPDATA%\DBYTEMEM\disk-watch\last-run.json
    %LOCALAPPDATA%\DBYTEMEM\disk-watch\transitions.jsonl

last-run records the latest complete probe. transitions records changes in worst drive status across OK, WARN and CRITICAL. it stores at most 1000 recent transitions. unchanged status does not append an event.

an error is exit 2 and never treated as OK. a corrupt previous state is rejected. the probe returns 0 for OK, 3 for WARN and 4 for CRITICAL, preserved by the watcher. state updates use same-directory temporary files and replacement.

for targeted tests set -DataDirectory to a dedicated folder and -NoNotify. the test suite uses only test-owned folders.

## install a user task

after native verification from integrated main, from a regular Windows PowerShell window:

    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\disk-watch-task-r6.ps1 -Action Install -EveryMinutes 30

inspect:

    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\disk-watch-task-r6.ps1 -Action Status

revoke:

    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\disk-watch-task-r6.ps1 -Action Uninstall

this is opt-in. task creation is not part of building, testing, or Git checkout. the installer refuses to overwrite an existing task or a different binary under the install path. it copies the SHA-256-verified native probe to:

    %LOCALAPPDATA%\DBYTEMEM\bin\dbmem_disk.exe

the task runs as the installing user with limited privileges and an interactive logon requirement. it starts at the next two-minute mark, then repeats every 15..1440 minutes as configured, for up to ten years. it has a two-minute execution limit and ignores overlapping starts. if no interactive user session exists it cannot run until a suitable logon/session. changing the source script path, uninstalling the binary or disconnecting the drive can cause a task failure; the task never silently treats it as disk OK.

the installer also copies the watch script into the same local bin directory with SHA-256 parity. the task points only to the two installed local files. moving or deleting the checkout does not invalidate the registered task. upgrades with mismatched installed hashes are rejected until reviewed and explicitly handled. uninstall leaves both installed files and evidence intact so existing logs are not destroyed. uninstallation does not change Windows disk policy.

## local alert behavior

on a new WARN or CRITICAL transition the watcher attempts a Windows tray balloon in the logged-on session. this depends on Windows notification settings and interactive desktop availability. a tray balloon appearing is not guaranteed and was not verified by the headless regression. no email, push, or external network message is sent. normal runs and repeated alert levels do not create new balloons. a recovery to OK is logged without a balloon.

the JSONL file is the durable record. operators should not treat a successful task registration as proof that a human saw a notification.

## testing

    powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_disk_watch_r6.ps1 -ProbePath build-msvc\dbmem_disk.exe -OutputRoot build-msvc

tests simulate warning levels only by changing percentage arguments. no disk is filled. acceptance is OK -> WARN -> WARN -> CRITICAL -> OK, four transition rows, no duplicate event, and denial of a malformed prior state. a separate Windows-native test checks registration, status and removal of the named test task.

## CI

GitHub Actions remains blocked at the account level. dispatching native-ci from main returned HTTP 422, "Actions has been disabled for this user" on 2026-10-23 despite repository Actions reporting enabled and the workflow active. local tests are not hosted CI evidence. see issue #4.
