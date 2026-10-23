# DBYTEMEM R6 opt-in local scheduled recovery watch
[CmdletBinding()]
param(
    [ValidateSet('Install','Uninstall','Status')][string]$Action='Status',
    [ValidateSet('DBYTEMEM Disk Watch','DBYTEMEM R6 Test Watch')][string]$TaskName='DBYTEMEM Disk Watch',
    [ValidateRange(15,1440)][int]$EveryMinutes=30,
    [string]$ProbePath='',
    [switch]$NoNotify
)
$ErrorActionPreference='Stop'
try {
    $existing=Get-ScheduledTask -TaskName $TaskName -TaskPath '\' -ErrorAction SilentlyContinue
    if($Action -eq 'Status'){
        if(!$existing){Write-Output 'task=NOT_INSTALLED';exit 0}
        Write-Output ('task='+$existing.TaskName+' state='+$existing.State)
        $existing.Actions|ForEach-Object {Write-Output ('execute='+$_.Execute+' arguments='+$_.Arguments)}
        exit 0
    }
    if($Action -eq 'Uninstall'){
        if(!$existing){Write-Output 'task=NOT_INSTALLED';exit 0}
        Unregister-ScheduledTask -TaskName $TaskName -TaskPath '\' -Confirm:$false
        if(Get-ScheduledTask -TaskName $TaskName -TaskPath '\' -ErrorAction SilentlyContinue){throw 'uninstall not complete'}
        Write-Output 'task=UNINSTALLED'
        Write-Output 'watch log and local binary retained'
        exit 0
    }
    if($existing){throw 'task already installed; refusing overwrite'}
    if(!$env:LOCALAPPDATA){throw 'LOCALAPPDATA missing'}
    $watchScript=Join-Path $PSScriptRoot 'disk-watch-r6.ps1'
    if(!(Test-Path -LiteralPath $watchScript -PathType Leaf)){throw 'watch script missing'}
    if(!$ProbePath){$ProbePath=Join-Path (Split-Path $PSScriptRoot -Parent) 'build-msvc\dbmem_disk.exe'}
    if(!(Test-Path -LiteralPath $ProbePath -PathType Leaf)){throw 'probe missing; run native verifier first'}
    $bin=Join-Path $env:LOCALAPPDATA 'DBYTEMEM\bin'
    [IO.Directory]::CreateDirectory($bin)|Out-Null
    $copy=Join-Path $bin 'dbmem_disk.exe'
    $sourceHash=(Get-FileHash -LiteralPath $ProbePath -Algorithm SHA256).Hash
    if(Test-Path -LiteralPath $copy){
        if((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $sourceHash){
            throw 'existing installed probe differs; refusing overwrite'
        }
    }else{
        Copy-Item -LiteralPath $ProbePath -Destination $copy -ErrorAction Stop
    }
    if((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $sourceHash){throw 'probe copy hash mismatch'}
    $watchCopy=Join-Path $bin 'disk-watch-r6.ps1'
    $watchHash=(Get-FileHash -LiteralPath $watchScript -Algorithm SHA256).Hash
    if(Test-Path -LiteralPath $watchCopy){
        if((Get-FileHash -LiteralPath $watchCopy -Algorithm SHA256).Hash -ne $watchHash){
            throw 'installed watch script differs; refusing overwrite'
        }
    } else {
        Copy-Item -LiteralPath $watchScript -Destination $watchCopy -ErrorAction Stop
    }
    if((Get-FileHash -LiteralPath $watchCopy -Algorithm SHA256).Hash -ne $watchHash){throw 'watch script hash mismatch'}
    $exe=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    if(!(Test-Path -LiteralPath $exe)){throw 'Windows PowerShell missing'}
    $arguments='-NoProfile -NonInteractive -Sta -ExecutionPolicy Bypass -File "'+$watchCopy+'" -ProbePath "'+$copy+'"'
    if($NoNotify){$arguments+=' -NoNotify'}
    $taskAction=New-ScheduledTaskAction -Execute $exe -Argument $arguments -WorkingDirectory $bin
    $trigger=New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(2) -RepetitionInterval ([TimeSpan]::FromMinutes($EveryMinutes)) -RepetitionDuration ([TimeSpan]::FromDays(3650))
    $user=[Security.Principal.WindowsIdentity]::GetCurrent().Name
    $principal=New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
    $settings=New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::FromMinutes(2)) -StartWhenAvailable
    Register-ScheduledTask -TaskName $TaskName -TaskPath '\' -Action $taskAction -Trigger $trigger -Principal $principal -Settings $settings | Out-Null
    $installed=Get-ScheduledTask -TaskName $TaskName -TaskPath '\' -ErrorAction Stop
    if(!$installed){throw 'task not observable after install'}
    Write-Output ('task=INSTALLED name='+$installed.TaskName+' user='+$user+' interval_minutes='+$EveryMinutes)
    Write-Output ('probe_sha256='+$sourceHash.ToLowerInvariant())
    Write-Output ('watch_sha256='+$watchHash.ToLowerInvariant())
    Write-Output 'interactive user logon required; unregister with -Action Uninstall'
}catch{
    Write-Error -Message ('disk_watch_task_error='+$_.Exception.Message) -ErrorAction Continue
    exit 2
}
