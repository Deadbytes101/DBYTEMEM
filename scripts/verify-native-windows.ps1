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
    @('tests\test_arena.c','arena_test',''),
    @('tests\test_vm_win.c','arena_win_test',''),
    @('tests\test_policy_contract.c','policy_contract_test',''),
    @('tests\test_series_args.c','series_args_test',''),
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
    if ($entry[2]) { $arguments += @('/link', $entry[2]) }
    & cl @arguments
    if ($LASTEXITCODE -ne 0) { throw ('build failed ' + $entry[1]) }
    $lines.Add('build_' + $entry[1] + '=PASS')
}
foreach ($n in @('arena_test','arena_win_test','policy_contract_test','policy_win_test','series_args_test','dbmem','bench_alloc_win')) {
    $exe = Join-Path $build ($n + '.exe')
    $output = & $env:ComSpec /d /s /c ('"' + $exe + '" 2>&1')
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw ('test failed ' + $n + ' exit=' + $code + [Environment]::NewLine + ($output -join [Environment]::NewLine)) }
    $lines.Add('test_' + $n + '=PASS')
    if ($n -eq 'bench_alloc_win') { foreach ($line in $output) { $lines.Add('bench_' + $line) } }
}
foreach ($n in @('dbmem_policy','dbmem')) {
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
foreach ($entry in $definitions) {
    $exe = Join-Path $build ($entry[1] + '.exe')
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash.ToLowerInvariant()
    $lines.Add('sha256_' + $entry[1] + '=' + $hash)
}
$lines.Add('native_gate=PASS')
$receipt = Join-Path $build 'NATIVE-VERIFY.txt'
[System.IO.File]::WriteAllLines($receipt, $lines, [System.Text.UTF8Encoding]::new($false))
Get-Content -LiteralPath $receipt
