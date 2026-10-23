# DBYTEMEM R6 read-only disk watch and bounded recovery journal
[CmdletBinding()]
param(
    [string]$ProbePath = '',
    [string]$DataDirectory = '',
    [ValidateRange(1,99)][int]$WarnPercent = 15,
    [ValidateRange(1,98)][int]$CriticalPercent = 10,
    [ValidateRange(0,1024)][int]$CriticalFreeGiB = 5,
    [switch]$NoNotify
)
$ErrorActionPreference = 'Stop'
$mutex = $null
$locked = $false
$code = 2

function Save-AtomicUtf8([string]$Path,[string]$Text) {
    $suffix = [guid]::NewGuid().ToString('N')
    $tmp = $Path + '.' + $suffix + '.tmp'
    $backup = $Path + '.' + $suffix + '.bak'
    try {
        [IO.File]::WriteAllText($tmp,$Text,[Text.UTF8Encoding]::new($false))
        if ([IO.File]::Exists($Path)) {
            [IO.File]::Replace($tmp,$Path,$backup)
        } else {
            [IO.File]::Move($tmp,$Path)
        }
    } finally {
        if ([IO.File]::Exists($tmp)) { [IO.File]::Delete($tmp) }
        if ([IO.File]::Exists($backup)) { [IO.File]::Delete($backup) }
    }
}
try {
    if ($CriticalPercent -ge $WarnPercent) { throw 'invalid thresholds' }
    if (!$ProbePath) {
        $ProbePath = Join-Path (Split-Path $PSScriptRoot -Parent) 'build-msvc\dbmem_disk.exe'
    }
    if (!$DataDirectory) {
        if (!$env:LOCALAPPDATA) { throw 'LOCALAPPDATA missing' }
        $DataDirectory = Join-Path $env:LOCALAPPDATA 'DBYTEMEM\disk-watch'
    }
    if (!(Test-Path -LiteralPath $ProbePath -PathType Leaf)) { throw 'probe missing' }
    [IO.Directory]::CreateDirectory($DataDirectory) | Out-Null
    $mutex = [Threading.Mutex]::new($false,'Local\DBYTEMEM_DiskWatch_R6')
    try {
        $locked = $mutex.WaitOne(0)
    } catch [Threading.AbandonedMutexException] {
        $locked = $true
    }
    if (!$locked) { throw 'watch already running' }
    $output = @(& $ProbePath --drive C --drive D --csv --warn-percent $WarnPercent --critical-percent $CriticalPercent --critical-free-gib $CriticalFreeGiB)
    $code = [int]$LASTEXITCODE
    if ($code -notin @(0,3,4)) { throw ('probe failed code='+$code) }
    $expected = 'drive,status,total_bytes_for_caller,free_bytes_for_caller,volume_free_bytes,free_percent'
    if ($output.Count -ne 3 -or $output[0] -cne $expected) { throw 'invalid probe csv' }
    $seen = @{}
    $drives = @()
    $worst = 0
    foreach ($row in $output[1..2]) {
        $v = $row.Split(',')
        if ($v.Count -ne 6 -or $v[0] -cnotin @('C:','D:') -or $seen.ContainsKey($v[0])) {
            throw 'invalid drive row'
        }
        $seen[$v[0]] = $true
        if ($v[1] -cnotin @('OK','WARN','CRITICAL')) { throw 'invalid disk status' }
        $bytes = @()
        foreach ($k in 2..4) {
            [uint64]$n = 0
            if (![uint64]::TryParse($v[$k],[ref]$n)) { throw 'invalid byte count' }
            $bytes += $n
        }
        if ($bytes[0] -eq 0 -or $bytes[1] -gt $bytes[0] -or
            $bytes[1] -gt $bytes[2]) { throw 'inconsistent byte count' }
        if ($v[5] -cnotmatch '^\d{1,3}\.\d{2}$') { throw 'invalid free percent' }
        $pct = [double]::Parse($v[5],[Globalization.CultureInfo]::InvariantCulture)
        if ($pct -gt 100.0) { throw 'free percent above 100' }
        if ($v[1] -eq 'CRITICAL') {$worst=4}
        elseif ($v[1] -eq 'WARN' -and $worst -ne 4) {$worst=3}
        $drives += [ordered]@{drive=$v[0];status=$v[1];free_bytes=[string]$bytes[1];total_bytes=[string]$bytes[0]}
    }
    if ($worst -ne $code) { throw 'probe exit status disagrees with rows' }
    $statePath = Join-Path $DataDirectory 'last-run.json'
    $historyPath = Join-Path $DataDirectory 'transitions.jsonl'
    $before = 'UNKNOWN'
    if ([IO.File]::Exists($statePath)) {
        $previous = [IO.File]::ReadAllText($statePath) | ConvertFrom-Json
        if ($previous.version -ne 1 -or $previous.status -cnotin @('OK','WARN','CRITICAL')) {
            throw 'invalid prior state'
        }
        $before = [string]$previous.status
    }
    $status = if($code -eq 4){'CRITICAL'}elseif($code -eq 3){'WARN'}else{'OK'}
    $now = [DateTime]::UtcNow.ToString('o')
    $state = [ordered]@{version=1;utc=$now;status=$status;exit_code=$code;drives=$drives}
    if ($status -cne $before) {
        $event = [ordered]@{utc=$now;from=$before;to=$status;drives=$drives}
        $line = ($event | ConvertTo-Json -Compress -Depth 6)
        $past = @()
        if ([IO.File]::Exists($historyPath)) {
            $past = @(Get-Content -LiteralPath $historyPath -Tail 999 -Encoding UTF8)
        }
        $lines = @($past | Where-Object { $_ -ne '' }) + @($line)
        Save-AtomicUtf8 $historyPath (($lines -join [Environment]::NewLine) + [Environment]::NewLine)
    }
    Save-AtomicUtf8 $statePath (($state | ConvertTo-Json -Compress -Depth 6) + [Environment]::NewLine)
    Write-Output ("status=$status previous=$before exit=$code")
    foreach ($d in $drives) { Write-Output ("drive="+$d.drive+" status="+$d.status+" free_bytes="+$d.free_bytes) }
    if (!$NoNotify -and $before -cne $status -and $status -ne 'OK') {
        try {
            Add-Type -AssemblyName System.Windows.Forms
            Add-Type -AssemblyName System.Drawing
            $tip = [Windows.Forms.NotifyIcon]::new()
            try {
                $tip.Icon = [Drawing.SystemIcons]::Warning
                $tip.Visible = $true
                $tip.BalloonTipTitle = 'DBYTEMEM disk '+$status
                $tip.BalloonTipText = (($drives | ForEach-Object {$_.drive+' '+$_.status}) -join ' | ')
                $tip.ShowBalloonTip(7000)
                Start-Sleep -Seconds 7
            } finally { $tip.Visible=$false; $tip.Dispose() }
        } catch {
            Write-Warning ('tray unavailable: '+$_.Exception.Message)
        }
    }
} catch {
    Write-Error -Message ('disk_watch_error='+$_.Exception.Message) -ErrorAction Continue
    $code = 2
} finally {
    if ($locked -and $mutex) { $mutex.ReleaseMutex() }
    if ($mutex) { $mutex.Dispose() }
}
exit $code
