# DBYTEMEM R6 native Windows recovery-watch regression
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ProbePath,
      [Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$script=Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts\disk-watch-r6.ps1'
if(!(Test-Path -LiteralPath $script -PathType Leaf)){throw 'watch script missing'}
if(!(Test-Path -LiteralPath $ProbePath -PathType Leaf)){throw 'native probe missing'}
$dir=Join-Path $OutputRoot ('r6-watch-test-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($dir)|Out-Null
$good=Join-Path $dir 'good'
$bad=Join-Path $dir 'invalid'
[IO.Directory]::CreateDirectory($good)|Out-Null
[IO.Directory]::CreateDirectory($bad)|Out-Null

function Invoke-Watch([string]$DataDir,[string[]]$Extra,[int]$Expected) {
    $cmd=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$script,
           '-ProbePath',$ProbePath,'-DataDirectory',$DataDir,'-NoNotify')+$Extra
    $oldPref=$ErrorActionPreference
    $ErrorActionPreference='Continue'
    try {
        $output=@(& powershell.exe @cmd 2>$null)
        $exit=[int]$LASTEXITCODE
    } finally {
        $ErrorActionPreference=$oldPref
    }
    if($exit -ne $Expected){throw ('watch exit expected '+$Expected+' actual '+$exit+' '+($output -join '|'))}
    return $output
}
$ok=@('-WarnPercent','2','-CriticalPercent','1','-CriticalFreeGiB','0')
$warn=@('-WarnPercent','99','-CriticalPercent','1','-CriticalFreeGiB','0')
$critical=@('-WarnPercent','99','-CriticalPercent','98','-CriticalFreeGiB','0')
$none=@(Invoke-Watch $good $ok 0)
$one=@(Invoke-Watch $good $warn 3)
$repeat=@(Invoke-Watch $good $warn 3)
$two=@(Invoke-Watch $good $critical 4)
$three=@(Invoke-Watch $good $ok 0)
if($none[0] -notmatch '^status=OK previous=UNKNOWN exit=0' -or
   $one[0] -notmatch '^status=WARN previous=OK exit=3' -or
   $repeat[0] -notmatch '^status=WARN previous=WARN exit=3' -or
   $two[0] -notmatch '^status=CRITICAL previous=WARN exit=4' -or
   $three[0] -notmatch '^status=OK previous=CRITICAL exit=0'){
    throw 'state transitions mismatch'
}
$events=@(Get-Content -LiteralPath (Join-Path $good 'transitions.jsonl')|ForEach-Object {$_|ConvertFrom-Json})
if($events.Count -ne 4){throw ('wrong event count '+$events.Count)}
if(($events|ForEach-Object {$_.to}) -join ',' -cne 'OK,WARN,CRITICAL,OK'){throw 'event order mismatch'}
$last=Get-Content -LiteralPath (Join-Path $good 'last-run.json') -Raw|ConvertFrom-Json
if($last.version -ne 1 -or $last.status -cne 'OK' -or $last.drives.Count -ne 2){throw 'last state invalid'}
$badState=Join-Path $bad 'last-run.json'
[IO.File]::WriteAllText($badState,'{"version":999,"status":"BROKEN"}',[Text.UTF8Encoding]::new($false))
$before=(Get-FileHash -Algorithm SHA256 -LiteralPath $badState).Hash
$broken=@(Invoke-Watch $bad $ok 2)
$after=(Get-FileHash -Algorithm SHA256 -LiteralPath $badState).Hash
if($before -ne $after){throw 'invalid state overwritten'}
if(Test-Path -LiteralPath (Join-Path $bad 'transitions.jsonl')){throw 'event created for invalid state'}
Write-Output 'watch_r6_sequence=PASS'
Write-Output 'watch_r6_no_duplicate_transition=PASS'
Write-Output 'watch_r6_invalid_prior_state=PASS'
Write-Output ('watch_r6_test_dir='+$dir)
