param(
    [ValidateSet('Editor', 'Content', 'Package', 'All')][string]$Stage = 'All',
    [string]$EngineRoot = '',
    [string]$ProjectAlias = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($ProjectAlias) {
    if (-not (Test-Path -LiteralPath $ProjectAlias)) {
        New-Item -ItemType Junction -Path $ProjectAlias -Target $projectRoot | Out-Null
    }
    $aliasItem = Get-Item -LiteralPath $ProjectAlias
    if ($aliasItem.LinkType -ne 'Junction' -or $aliasItem.Target -ne $projectRoot) {
        throw 'ProjectAlias must be a junction to this project.'
    }
    $projectRoot = $ProjectAlias
}
$engineCandidates = @($EngineRoot, $env:UE_ENGINE_ROOT, $env:UE5_ROOT) | Where-Object { $_ }
if (-not $engineCandidates) {
    $editorCommand = Get-Command UnrealEditor.exe, UnrealEditor -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($editorCommand) {
        $engineCandidates = @(Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $editorCommand.Source))))
    }
}
$EngineRoot = $engineCandidates | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'Engine\Build\BatchFiles\Build.bat') } | Select-Object -First 1
if (-not $EngineRoot) {
    throw 'Unreal Engine 5.8 was not found. Pass -EngineRoot, or set UE_ENGINE_ROOT/UE5_ROOT.'
}
$projectFile = Join-Path $projectRoot 'BlockRealm.uproject'
$toolsRoot = Join-Path $projectRoot 'Tools'
$logRoot = Join-Path $projectRoot 'Deliverables\Validation'
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$previousSdkRoot = $env:UE_SDKS_ROOT
$localSdkRoot = $env:UE_AUTOSDK_ROOT
if ($localSdkRoot -and (Test-Path -LiteralPath $localSdkRoot)) { $env:UE_SDKS_ROOT = $localSdkRoot }

try {

if ($Stage -in @('Editor', 'All')) {
    & "$EngineRoot\Engine\Build\BatchFiles\Build.bat" BlockRealmEditor Win64 Development "-Project=$projectFile" -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=2
    if ($LASTEXITCODE -ne 0) { throw "Editor build failed: $LASTEXITCODE" }
}

if ($Stage -in @('Content', 'All')) {
    $editorArguments = @("`"$projectFile`"", "`"-ExecutePythonScript=$toolsRoot\build_content.py`"", '-unattended', '-nop4', '-nosplash', '-nullrhi', "`"-abslog=$logRoot\content-import.log`"")
    $editorProcess = Start-Process -FilePath "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $editorArguments -WindowStyle Hidden -PassThru
    $editorProcess.WaitForExit()
    $contentReport = Get-Content -Raw -LiteralPath (Join-Path $logRoot 'content-import.json') | ConvertFrom-Json
    if ($editorProcess.ExitCode -ne 0 -or -not $contentReport.success) { throw "Content import failed; see content-import.log" }
}

if ($Stage -in @('Package', 'All')) {
    $archiveRoot = Join-Path $projectRoot 'Deliverables'
    & "$EngineRoot\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$projectFile" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -iostore -archive "-archivedirectory=$archiveRoot" -unattended -utf8output -nocompileeditor -nodebuginfo -prereqs "-UbtArgs=-MaxParallelActions=2"
    if ($LASTEXITCODE -ne 0) { throw "Windows packaging failed: $LASTEXITCODE" }
    $licenseRoot = Join-Path $archiveRoot 'Windows\Licenses'
    New-Item -ItemType Directory -Force -Path $licenseRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot 'RawAssets\Fonts\MiSans-License-Official.pdf') -Destination $licenseRoot
    Copy-Item -LiteralPath (Join-Path $projectRoot 'RawAssets\Fonts\COPYRIGHT.txt') -Destination $licenseRoot
    Copy-Item -LiteralPath (Join-Path $projectRoot 'Docs\AssetSources.md') -Destination $licenseRoot
    Copy-Item -LiteralPath (Join-Path $projectRoot 'Plugins\DLSS\Source\ThirdParty\NGX\LICENSE.txt') -Destination (Join-Path $licenseRoot 'NVIDIA-DLSS-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'Docs\Licenses\NVIDIA-STREAMLINE-MIT-NOTICES.txt') -Destination $licenseRoot
    Copy-Item -LiteralPath (Join-Path $projectRoot 'Docs\Licenses\NVIDIA RTX SDK EULA.pdf') -Destination $licenseRoot
    if (Test-Path -LiteralPath (Join-Path $projectRoot 'README.md')) {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination (Join-Path $archiveRoot 'Windows\README.md')
    }
    $docsRoot = Join-Path $archiveRoot 'Windows\Docs'
    New-Item -ItemType Directory -Force -Path $docsRoot | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Docs') -Filter '*.md' -File |
        Copy-Item -Destination $docsRoot
}
}
finally { $env:UE_SDKS_ROOT = $previousSdkRoot }
