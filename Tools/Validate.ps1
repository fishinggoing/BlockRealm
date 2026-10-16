param(
    [int]$Width = 1920,
    [int]$Height = 1080,
    [string]$OutputName = '1920x1080',
    [string[]]$ExtraArguments = @(),
    [switch]$FreshWorld
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $projectRoot "Deliverables\Validation\$OutputName"
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$runtimeFile = Join-Path $outputRoot 'runtime.json'
if (Test-Path -LiteralPath $runtimeFile) { Remove-Item -LiteralPath $runtimeFile }
$executable = Join-Path $projectRoot 'Deliverables\Windows\BlockRealm.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Package the project first.' }
if ($FreshWorld) {
    $testSave = Join-Path $projectRoot 'Deliverables\Windows\BlockRealm\Saved\ValidationWorld\BlockRealmWorld.json'
    if (Test-Path -LiteralPath $testSave) { Remove-Item -LiteralPath $testSave }
}
$runArguments = @('-BlockRealmSmoke', "`"-BRValidationDir=$outputRoot`"", '-windowed', "-ResX=$Width", "-ResY=$Height", '-ForceRes', '-nosplash', '-unattended', "`"-abslog=$outputRoot\runtime.log`"")
$runArguments += $ExtraArguments
$startedAt = Get-Date
$runtimeProcess = Start-Process -FilePath $executable -ArgumentList $runArguments -WindowStyle Hidden -PassThru
if (-not $runtimeProcess.WaitForExit(300000)) {
    Stop-Process -Id $runtimeProcess.Id
    throw 'Runtime validation timed out.'
}
if ($runtimeProcess.ExitCode -ne 0) { throw "Runtime process failed: $($runtimeProcess.ExitCode)" }
if (-not (Test-Path -LiteralPath $runtimeFile)) { throw 'Fresh runtime report missing.' }
if ((Get-Item -LiteralPath $runtimeFile).LastWriteTime -lt $startedAt) { throw 'Runtime report is stale.' }
$report = Get-Content -Raw -LiteralPath $runtimeFile | ConvertFrom-Json
if (-not $report.success) { throw "Runtime checks failed: $($report.failed -join '; ')" }
if ($report.width -ne $Width -or $report.height -ne $Height) { throw 'Actual viewport dimensions do not match the request.' }
$screenshots = @(Get-ChildItem -LiteralPath $outputRoot -Filter '*.png' | Where-Object { $_.LastWriteTime -ge $startedAt })
if ($screenshots.Count -lt 6) { throw 'Expected six runtime screenshots.' }
if (Select-String -LiteralPath (Join-Path $outputRoot 'runtime.log') -Pattern 'Fatal error:|Assertion failed:|Unhandled Exception:' -Quiet) { throw 'Runtime log contains a fatal error.' }
Write-Output "Runtime checks passed: $($report.passed.Count); DLSS supported: $($report.dlss_supported); screenshots: $($screenshots.Count)"
Write-Output $report.graphics_status
