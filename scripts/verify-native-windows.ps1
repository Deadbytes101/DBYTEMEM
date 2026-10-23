# DBYTEMEM native verifier - run in Visual Studio x64 Developer PowerShell
# builds only test-owned binaries inside build-msvc
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build-msvc'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'cl.exe missing - use VS x64 Native Tools' }
New-Item -ItemType Directory -Force -Path $build | Out-Null
$definitions = @(
    @('src\dbmem.c','dbmem','psapi.lib'),
    @('src\dbmem_policy.c','dbmem_policy',''),
    @('src\dbmem_series.c','dbmem_series','psapi.lib'),
    @('src\dbmem_disk.c','dbmem_disk',''),
    @('src\dbmem_watch.c','dbmem_watch','shell32.lib user32.lib'),
    @('src\dbmem_pressure.c','dbmem_pressure','psapi.lib'),
    @('src\dbmem_pressure_watch.c','dbmem_pressure_watch','psapi.lib shell32.lib user32.lib'),
    @('src\dbmem_budget.c','dbmem_budget',''),
    @('tests\test_budget_child.c','budget_child',''),
    @('tests\test_budget_quoting.c','budget_quoting_test',''),
    @('tests\test_arena.c','arena_test',''),
    @('tests\test_vm_win.c','arena_win_test',''),
    @('tests\test_policy_contract.c','policy_contract_test',''),
    @('tests\test_series_args.c','series_args_test',''),
    @('tests\test_disk_policy.c','disk_policy_test',''),
    @('tests\test_pressure_policy.c','pressure_policy_test',''),
    @('tests\test_policy_win.c','policy_win_test',''),
    @('bench\bench_alloc_win.c','bench_alloc_win','')
)
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('verification=DBYTEMEM_NATIVE_WINDOWS')
$lines.Add('source_head=' + (& git -C $root rev-parse HEAD))
foreach ($entry in $definitions) {
    $src = Join-Path $root $entry[0]
    $exe = Join-Path $build ($entry[1] + '.exe')
    $obj = Join-Path $build ($entry[1] + '.obj')
    $arguments = @('/nologo','/std:c11','/W4','/WX','/O2','/TC', $src, '/Fe:' + $exe, '/Fo:' + $obj)
    if ($entry[2]) { $arguments += '/link'; $arguments += ($entry[2] -split ' ') }
    & cl @arguments
    if ($LASTEXITCODE -ne 0) { throw ('build failed ' + $entry[1]) }
    $lines.Add('build_' + $entry[1] + '=PASS')
}
foreach ($n in @('arena_test','arena_win_test','policy_contract_test','policy_win_test','series_args_test','disk_policy_test','pressure_policy_test','dbmem','bench_alloc_win')) {
    $exe = Join-Path $build ($n + '.exe')
    $output = & $env:ComSpec /d /s /c ('"' + $exe + '" 2>&1')
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw ('test failed ' + $n + ' exit=' + $code + [Environment]::NewLine + ($output -join [Environment]::NewLine)) }
    $lines.Add('test_' + $n + '=PASS')
    if ($n -eq 'bench_alloc_win') { foreach ($line in $output) { $lines.Add('bench_' + $line) } }
}
foreach ($n in @('dbmem_policy','dbmem','dbmem_disk','dbmem_watch','dbmem_pressure','dbmem_pressure_watch','dbmem_budget')) {
    $exe = Join-Path $build ($n + '.exe')
    & $env:ComSpec /d /s /c ('"' + $exe + '" --help >NUL 2>&1')
    if ($LASTEXITCODE -ne 0) { throw ('help failed ' + $n) }
}
$lines.Add('help_check=PASS')
$seriesPath = Join-Path $build 'dbmem_series.exe'
$series = @(& $seriesPath --samples 3 --interval-ms 100 --phase unclassified)
if ($LASTEXITCODE -ne 0 -or $series.Count -ne 4) { throw 'series row count failed' }
if ($series[0] -notmatch '^sample,phase,elapsed_ms,utc_filetime_100ns,') { throw 'series header failed' }
$previous = -1
for ($idx = 1; $idx -le 3; ++$idx) {
    $cols = $series[$idx].Split(',')
    if ($cols.Count -ne 13 -or $cols[0] -ne [string]$idx -or $cols[1] -ne 'unclassified') { throw 'series columns failed' }
    [long]$elapsed = 0
    if (-not [long]::TryParse($cols[2], [ref]$elapsed) -or $elapsed -lt $previous) { throw 'series clock failed' }
    $previous = $elapsed
    foreach ($column in 3..12) {
        [uint64]$num = 0
        if (-not [uint64]::TryParse($cols[$column], [ref]$num)) { throw 'series metric not unsigned integer' }
    }
    if ([uint64]::Parse($cols[5]) -gt [uint64]::Parse($cols[4])) { throw 'available exceeds total' }
    if ([uint64]::Parse($cols[8]) -gt [uint64]::Parse($cols[9])) { throw 'commit exceeds limit' }
}
& $env:ComSpec /d /s /c ('"' + $seriesPath + '" --samples 0 >NUL 2>&1')
if ($LASTEXITCODE -eq 0) { throw 'series invalid argument accepted' }
$lines.Add('test_dbmem_series=PASS')
$diskPath = Join-Path $build 'dbmem_disk.exe'
$diskRows = @(& $diskPath --csv)
if ($LASTEXITCODE -notin @(0,3,4) -or $diskRows.Count -lt 2) { throw 'disk scan failed' }
if ($diskRows[0] -cne 'drive,status,total_bytes_for_caller,free_bytes_for_caller,volume_free_bytes,free_percent') { throw 'disk csv header failed' }
foreach ($row in $diskRows[1..($diskRows.Count-1)]) {
    $cols = $row.Split(',')
    if ($cols.Count -ne 6 -or $cols[0] -cnotmatch '^[A-Z]:$' -or $cols[1] -cnotin @('OK','WARN','CRITICAL')) { throw 'disk csv schema failed' }
    $metrics = @()
    foreach ($column in 2..4) {
        [uint64]$num = 0
        if (-not [uint64]::TryParse($cols[$column],[ref]$num)) { throw 'disk csv integer failed' }
        $metrics += $num
    }
    if ($metrics[0] -eq 0 -or $metrics[1] -gt $metrics[0] -or $metrics[1] -gt $metrics[2]) { throw 'disk capacity invariant failed' }
    if ($cols[5] -cnotmatch '^\d+\.\d{2}$') { throw 'disk percent format failed' }
}
& $env:ComSpec /d /s /c ('"' + $diskPath + '" --warn-percent 0 >NUL 2>&1')
if ($LASTEXITCODE -ne 1) { throw 'disk invalid options not rejected' }
$lines.Add('test_dbmem_disk=PASS')
$watch = Join-Path $build 'dbmem_watch.exe'
$r6Dir = Join-Path $build ('r6-native-test-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($r6Dir) | Out-Null
$okArgs = @('--warn-percent','2','--critical-percent','1','--critical-free-gib','0')
$warnArgs = @('--warn-percent','99','--critical-percent','1','--critical-free-gib','0')
$criticalArgs = @('--warn-percent','99','--critical-percent','98','--critical-free-gib','0')
function Test-R6-Sample([string[]]$options,[int]$expected,[string]$from,[string]$to) {
    $rows = @(& $watch --no-notify --state-dir $r6Dir @options)
    $exit = $LASTEXITCODE
    if ($exit -ne $expected -or $rows.Count -ne 3 -or
        $rows[0] -cne ('status=' + $to + ' previous=' + $from)) {
        throw ('R6 transition failure ' + $from + ' to ' + $to + ' exit=' + $exit)
    }
}
Test-R6-Sample $okArgs 0 'UNKNOWN' 'OK'
Test-R6-Sample $warnArgs 3 'OK' 'WARN'
Test-R6-Sample $warnArgs 3 'WARN' 'WARN'
Test-R6-Sample $criticalArgs 4 'WARN' 'CRITICAL'
Test-R6-Sample $okArgs 0 'CRITICAL' 'OK'
$events = @(Get-Content -LiteralPath (Join-Path $r6Dir 'transitions.csv'))
if ($events.Count -ne 4) { throw 'R6 duplicate or missing transition' }
$expectedTransitions = @('UNKNOWN,OK','OK,WARN','WARN,CRITICAL','CRITICAL,OK')
for ($i=0;$i -lt 4;$i++) {
    $parts = $events[$i].Split(',')
    if ($parts.Count -ne 5 -or ($parts[1] + ',' + $parts[2]) -cne $expectedTransitions[$i]) {
        throw ('R6 transition journal malformed row=' + $i)
    }
    foreach ($column in @(0,3,4)) {
        [uint64]$num = 0
        if (-not [uint64]::TryParse($parts[$column],[ref]$num)) { throw 'R6 journal invalid number' }
    }
}
$state = (Get-Content -LiteralPath (Join-Path $r6Dir 'state.txt') -Raw).Trim()
if ($state -cne 'DBYTEMEM_R6_V1 OK') { throw 'R6 state mismatch' }
$badDir = Join-Path $r6Dir 'corrupt'
[IO.Directory]::CreateDirectory($badDir) | Out-Null
$badState = Join-Path $badDir 'state.txt'
[IO.File]::WriteAllText($badState,'corrupt',[Text.Encoding]::ASCII)
$priorHash = (Get-FileHash -LiteralPath $badState -Algorithm SHA256).Hash
& $env:ComSpec /d /s /c ('"' + $watch + '" --no-notify --state-dir "' + $badDir + '" --warn-percent 2 --critical-percent 1 --critical-free-gib 0 >NUL 2>&1')
if ($LASTEXITCODE -ne 2 -or (Get-FileHash -LiteralPath $badState -Algorithm SHA256).Hash -ne $priorHash -or
    (Test-Path -LiteralPath (Join-Path $badDir 'transitions.csv'))) {
    throw 'R6 corrupt state not denied'
}
& $env:ComSpec /d /s /c ('"' + $watch + '" --no-notify --state-dir "' + $r6Dir + '" --warn-percent +15 >NUL 2>&1')
if ($LASTEXITCODE -ne 1) { throw 'R6 malformed CLI accepted' }
& $env:ComSpec /d /s /c ('"' + $watch + '" --no-notify --state-dir "' + $r6Dir + '" --warn-percent 20 --warn-percent 21 >NUL 2>&1')
if ($LASTEXITCODE -ne 1) { throw 'R6 duplicate CLI accepted' }
$capDir = Join-Path $build ('r6-cap-test-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($capDir) | Out-Null
for ($j = 0; $j -lt 106; ++$j) {
    $opts = if (($j % 2) -eq 0) { $criticalArgs } else { $okArgs }
    & $watch --no-notify --state-dir $capDir @opts | Out-Null
    $expectedCode = if (($j % 2) -eq 0) { 4 } else { 0 }
    if ($LASTEXITCODE -ne $expectedCode) { throw ('R6 ring test failed iteration=' + $j) }
}
if (@(Get-Content -LiteralPath (Join-Path $capDir 'transitions.csv')).Count -ne 100) {
    throw 'R6 journal ring exceeded 100'
}
$badJournal = Join-Path $r6Dir 'corrupt-journal'
[IO.Directory]::CreateDirectory($badJournal) | Out-Null
& $watch --no-notify --state-dir $badJournal @okArgs | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'R6 journal fault baseline failed' }
$badJournalState = Join-Path $badJournal 'state.txt'
$badJournalHash = (Get-FileHash -LiteralPath $badJournalState -Algorithm SHA256).Hash
[IO.File]::WriteAllText((Join-Path $badJournal 'transitions.csv'),'truncated',[Text.Encoding]::ASCII)
& $env:ComSpec /d /s /c ('"' + $watch + '" --no-notify --state-dir "' + $badJournal + '" --warn-percent 99 --critical-percent 1 --critical-free-gib 0 >NUL 2>&1')
if ($LASTEXITCODE -ne 2 -or
    (Get-FileHash -LiteralPath $badJournalState -Algorithm SHA256).Hash -ne $badJournalHash) {
    throw 'R6 malformed journal not denied'
}
$lines.Add('test_dbmem_watch=PASS')
$pressure = Join-Path $build 'dbmem_pressure.exe'
$pressureResult = @(& $pressure --interval-ms 100)
if ($LASTEXITCODE -notin @(0,3,4) -or $pressureResult.Count -ne 10 -or
    $pressureResult[1] -cnotmatch '^status=(OK|WARN|CRITICAL)$' -or
    $pressureResult[2] -cnotmatch '^sample_levels=(OK|WARN|CRITICAL),(OK|WARN|CRITICAL),(OK|WARN|CRITICAL)$') {
    throw 'R7 live memory pressure probe invalid'
}
$kv=@{}
foreach($row in $pressureResult) {
    $pair=$row.Split('=',2)
    if($pair.Count -ne 2 -or $kv.ContainsKey($pair[0])) { throw 'R7 invalid key/value output' }
    $kv[$pair[0]]=$pair[1]
}
$numbers=@('physical_total_bytes','physical_available_bytes','commit_total_pages','commit_limit_pages','commit_headroom_pages','memory_load_percent','sample_interval_ms','utc_filetime_100ns')
foreach($name in $numbers) {
    [uint64]$n=0
    if(-not [uint64]::TryParse($kv[$name],[ref]$n)) { throw ('R7 invalid number '+$name) }
}
if ([uint64]$kv.physical_available_bytes -gt [uint64]$kv.physical_total_bytes -or
    [uint64]$kv.commit_total_pages -gt [uint64]$kv.commit_limit_pages -or
    ([uint64]$kv.commit_limit_pages - [uint64]$kv.commit_total_pages) -ne [uint64]$kv.commit_headroom_pages) {
    throw 'R7 snapshot invariants failed'
}
$forcedWarn = @(& $pressure --interval-ms 100 --warn-physical-percent 99 --critical-physical-percent 1 --warn-commit-headroom-percent 99 --critical-commit-headroom-percent 1)
if ($LASTEXITCODE -ne 3 -or $forcedWarn -cnotcontains 'status=WARN') {
    throw 'R7 forced WARN contract failed'
}
$forcedCritical = @(& $pressure --interval-ms 100 --warn-physical-percent 99 --critical-physical-percent 98 --warn-commit-headroom-percent 99 --critical-commit-headroom-percent 98)
if ($LASTEXITCODE -ne 4 -or $forcedCritical -cnotcontains 'status=CRITICAL') {
    throw 'R7 forced CRITICAL contract failed'
}
foreach($bad in @('--interval-ms 99','--interval-ms 1001','--interval-ms +250','--warn-physical-percent 0',
                  '--warn-physical-percent 20 --critical-physical-percent 20',
                  '--warn-physical-percent 20 --warn-physical-percent 30')) {
    & $env:ComSpec /d /s /c ('"' + $pressure + '" ' + $bad + ' >NUL 2>&1')
    if ($LASTEXITCODE -ne 1) { throw ('R7 invalid CLI accepted '+$bad) }
}
$lines.Add('test_dbmem_pressure=PASS')
$watch8=Join-Path $build 'dbmem_pressure_watch.exe'
$r8Dir=Join-Path $build ('r8-pressure-watch-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($r8Dir)|Out-Null
$r8Ok=@('--interval-ms','100','--warn-physical-percent','2','--critical-physical-percent','1',
        '--warn-commit-headroom-percent','2','--critical-commit-headroom-percent','1')
$r8Warn=@('--interval-ms','100','--warn-physical-percent','99','--critical-physical-percent','1',
          '--warn-commit-headroom-percent','99','--critical-commit-headroom-percent','1')
$r8Critical=@('--interval-ms','100','--warn-physical-percent','99','--critical-physical-percent','98',
              '--warn-commit-headroom-percent','99','--critical-commit-headroom-percent','98')
function Test-R8([string[]]$opts,[int]$code,[string]$status,[string]$previous) {
    $o=@(& $watch8 --no-notify --state-dir $r8Dir @opts)
    if($LASTEXITCODE -ne $code -or $o.Count -ne 5 -or
       $o[0] -cne ('status='+$status) -or
       $o[1] -cne ('previous='+$previous)) {
        throw ('R8 transition invalid expected '+$previous+' -> '+$status+' rc='+$LASTEXITCODE)
    }
}
Test-R8 $r8Ok 0 'OK' 'UNKNOWN'
Test-R8 $r8Warn 3 'WARN' 'OK'
Test-R8 $r8Warn 3 'WARN' 'WARN'
Test-R8 $r8Critical 4 'CRITICAL' 'WARN'
Test-R8 $r8Ok 0 'OK' 'CRITICAL'
$r8File=Join-Path $r8Dir 'state.txt'
$r8Lines=@(Get-Content -LiteralPath $r8File)
if($r8Lines[0] -cne 'DBMEM_PRESSURE_WATCH_V1 OK' -or
   @($r8Lines|Where-Object {$_ -like 'EVENT *'}).Count -ne 4) {
    throw 'R8 journal transition or state malformed'
}
$r8Bad=Join-Path $r8Dir 'corrupt'
[IO.Directory]::CreateDirectory($r8Bad)|Out-Null
$r8BadFile=Join-Path $r8Bad 'state.txt'
[IO.File]::WriteAllText($r8BadFile,'garbled',[Text.Encoding]::ASCII)
$badHash=(Get-FileHash -LiteralPath $r8BadFile -Algorithm SHA256).Hash
& $env:ComSpec /d /s /c ('"' + $watch8 + '" --no-notify --state-dir "'+$r8Bad+'" --interval-ms 100 --warn-physical-percent 2 --critical-physical-percent 1 --warn-commit-headroom-percent 2 --critical-commit-headroom-percent 1 >NUL 2>&1')
if($LASTEXITCODE -ne 2 -or (Get-FileHash -LiteralPath $r8BadFile -Algorithm SHA256).Hash -ne $badHash) {
    throw 'R8 corrupted state accepted or overwritten'
}
$originalR8=(Get-Content -LiteralPath $r8File -Raw)
foreach($case in @(
    @('(?m)^SNAPSHOT \d+','SNAPSHOT -1'),
    @('(?m)^EVENT \d+','EVENT -1'),
    @('(?m)^EVENT \d+','EVENT 18446744073709551616')
)) {
    $mutated=$originalR8 -replace $case[0],$case[1]
    if($mutated -ceq $originalR8){throw 'R8 lexical fixture not mutated'}
    [IO.File]::WriteAllText($r8BadFile,$mutated,[Text.UTF8Encoding]::new($false))
    $before=(Get-FileHash -LiteralPath $r8BadFile -Algorithm SHA256).Hash
    & $env:ComSpec /d /s /c ('"' + $watch8 + '" --no-notify --state-dir "'+$r8Bad+'" --interval-ms 100 --warn-physical-percent 2 --critical-physical-percent 1 --warn-commit-headroom-percent 2 --critical-commit-headroom-percent 1 >NUL 2>&1')
    if($LASTEXITCODE -ne 2 -or (Get-FileHash -LiteralPath $r8BadFile -Algorithm SHA256).Hash -ne $before) {
        throw ('R8 malformed numeric journal accepted '+$case[1])
    }
}
foreach($bad in @('--interval-ms 0','--interval-ms +100','--interval-ms 100 --interval-ms 100',
                  '--warn-physical-percent 20 --critical-physical-percent 20')) {
    & $env:ComSpec /d /s /c ('"' + $watch8 + '" ' + $bad + ' >NUL 2>&1')
    if($LASTEXITCODE -ne 1) { throw ('R8 bad CLI accepted '+$bad) }
}
for($i=0;$i -lt 102;$i++){
    $opts=if(($i%2)-eq 0){$r8Critical}else{$r8Ok}
    & $watch8 --no-notify --state-dir $r8Dir @opts | Out-Null
    $expected=if(($i%2)-eq 0){4}else{0}
    if($LASTEXITCODE -ne $expected){throw ('R8 ring run failed '+$i)}
}
$r8Lines=@(Get-Content -LiteralPath $r8File)
if(@($r8Lines|Where-Object {$_ -like 'EVENT *'}).Count -ne 100 -or
   $r8Lines[0] -cne 'DBMEM_PRESSURE_WATCH_V1 OK') {
    throw 'R8 bounded journal ring failure'
}
$lines.Add('test_dbmem_pressure_watch=PASS')
$budget=Join-Path $build 'dbmem_budget.exe'
$budgetChild=Join-Path $build 'budget_child.exe'
$budgetQuote=Join-Path $build 'budget_quoting_test.exe'
$plan=@(& $budget plan --limit-mib 256 --exe $budgetChild -- --alloc-mib 80)
if($LASTEXITCODE -ne 0 -or $plan -cnotcontains 'mutation=none' -or
   $plan -cnotcontains 'job_commit_limit_bytes=268435456') {
    throw 'R9 dry-run contract failed'
}
$allowed=@(& $budget run --limit-mib 256 --exe $budgetChild --confirm -- --alloc-mib 80)
if($LASTEXITCODE -ne 0 -or $allowed -cnotcontains 'child_exit_code=0' -or
   $allowed -cnotcontains 'allocation_succeeded_bytes=83886080' -or
   $allowed -cnotcontains 'job_closed=1') {
    throw 'R9 allowed allocation contract failed'
}
$denied=@(& $budget run --limit-mib 64 --exe $budgetChild --confirm -- --alloc-mib 80)
if($LASTEXITCODE -ne 3 -or $denied -cnotcontains 'child_exit_code=73' -or
   $denied -cnotcontains 'allocation_denied=1') {
    throw 'R9 limit enforcement contract failed'
}
$quoted=@(& $budgetQuote $budgetChild)
if($LASTEXITCODE -ne 0 -or $quoted -cnotcontains 'argument_quoting=PASS' -or
   $quoted -cnotcontains 'child_exit_code=0') {
    throw 'R9 Windows native quoting contract failed'
}
foreach($bad in @(
 'run --limit-mib 256 --exe "'+$budgetChild+'" -- --alloc-mib 80',
 'plan --limit-mib 12 --exe "'+$budgetChild+'"',
 'plan --limit-mib 256 --limit-mib 512 --exe "'+$budgetChild+'"',
 'plan --limit-mib 256 --exe "Z:\missing\no.exe"'
)) {
    & $env:ComSpec /d /s /c ('"' + $budget + '" ' + $bad + ' >NUL 2>&1')
    if($LASTEXITCODE -ne 1) {
        throw ('R9 negative CLI accepted '+$bad)
    }
}
$lines.Add('test_dbmem_budget=PASS')
foreach ($entry in $definitions) {
    $exe = Join-Path $build ($entry[1] + '.exe')
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash.ToLowerInvariant()
    $lines.Add('sha256_' + $entry[1] + '=' + $hash)
}
$lines.Add('native_gate=PASS')
$receipt = Join-Path $build 'NATIVE-VERIFY.txt'
[System.IO.File]::WriteAllLines($receipt, $lines, [System.Text.UTF8Encoding]::new($false))
Get-Content -LiteralPath $receipt
