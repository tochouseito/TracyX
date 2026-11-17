param(
    [string]$BuildDirectory = "build/tracyx-m3",
    [int]$Frames = 600,
    [int]$Repetitions = 3,
    [int]$MaxEvents = 1000000,
    [string[]]$Modes = @('Light', 'System', 'Deep'),
    [string]$OutputDirectory = "$env:TEMP/TracyX-overhead"
)

$ErrorActionPreference = 'Stop'
$build = (Resolve-Path $BuildDirectory).Path
$probe = Join-Path $build 'Debug/TracyXProbePresentD3D12.exe'
$agent = Join-Path $build 'Debug/TracyXAgent.exe'
if (!(Test-Path $probe) -or !(Test-Path $agent)) { throw 'Build the probe and Agent first.' }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$rows = @()
foreach ($mode in $Modes) {
    if ($mode -notin @('Light', 'System', 'Deep')) { throw "Unknown mode: $mode" }
    for ($run = 1; $run -le $Repetitions; $run++) {
        $directory = Join-Path $OutputDirectory "$mode-$run"
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
        $arguments = @($Frames, $directory)
        if ($mode -eq 'Deep') { $arguments += 'gpu' }
        $clock = [System.Diagnostics.Stopwatch]::StartNew()
        $target = Start-Process -FilePath $probe -ArgumentList $arguments -PassThru `
            -RedirectStandardOutput (Join-Path $directory 'probe.out') `
            -RedirectStandardError (Join-Path $directory 'probe.err')
        $collector = $null
        if ($mode -ne 'Light') {
            Start-Sleep -Milliseconds 300
            if (!$target.HasExited) {
                $collector = Start-Process -FilePath $agent -ArgumentList @($target.Id, $directory, $MaxEvents) `
                    -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $directory 'agent.out') `
                    -RedirectStandardError (Join-Path $directory 'agent.err')
            }
        }
        $peakBytes = [int64]0
        $cpuSeconds = 0.0
        $collectorPeakBytes = [int64]0
        $collectorCpuSeconds = 0.0
        while (!$target.HasExited) {
            try {
                $sample = Get-Process -Id $target.Id -ErrorAction Stop
                $peakBytes = [Math]::Max($peakBytes, [int64]$sample.WorkingSet64)
                $cpuSeconds = $sample.CPU
            } catch { }
            if ($collector) {
                try {
                    $agentSample = Get-Process -Id $collector.Id -ErrorAction Stop
                    $collectorPeakBytes = [Math]::Max($collectorPeakBytes, [int64]$agentSample.WorkingSet64)
                    $collectorCpuSeconds = $agentSample.CPU
                } catch { }
            }
            Start-Sleep -Milliseconds 100
            $target.Refresh()
        }
        $target.WaitForExit()
        $clock.Stop()
        if ($collector) { $collector.WaitForExit() }
        $manifest = $null
        $manifestFile = Join-Path $directory 'session.json'
        if (Test-Path $manifestFile) { $manifest = Get-Content $manifestFile -Raw | ConvertFrom-Json }
        $storageBytes = (Get-ChildItem $directory -File -Recurse | Measure-Object Length -Sum).Sum
        $rows += [pscustomobject]@{
            mode = $mode; run = $run; frames = $Frames; elapsed_ms = [Math]::Round($clock.Elapsed.TotalMilliseconds, 2)
            ms_per_frame = [Math]::Round($clock.Elapsed.TotalMilliseconds / $Frames, 3)
            target_cpu_s = [Math]::Round($cpuSeconds, 3); target_peak_mb = [Math]::Round($peakBytes / 1MB, 2)
            agent_cpu_s = [Math]::Round($collectorCpuSeconds, 3)
            agent_peak_mb = [Math]::Round($collectorPeakBytes / 1MB, 2)
            storage_mb = [Math]::Round($storageBytes / 1MB, 2)
            target_exit = $target.ExitCode; agent_exit = if ($collector) { $collector.ExitCode } else { $null }
            events = if ($manifest) { $manifest.event_count } else { $null }
            etw_lost = if ($manifest) { $manifest.lost_events } else { $null }
            queue_drops = if ($manifest) { $manifest.queue_drops } else { $null }
            bridge_missing = if ($manifest) { $manifest.bridge_missing } else { $null }
        }
    }
}
$rows | Export-Csv (Join-Path $OutputDirectory 'measurements.csv') -NoTypeInformation
$rows | Format-Table -AutoSize
