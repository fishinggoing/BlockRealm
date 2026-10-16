param(
    [string]$OutputFile = '',
    [switch]$IncludeWorldSave
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$packageRoot = (Resolve-Path -LiteralPath (Join-Path $projectRoot 'Deliverables\Windows')).Path
$worldSaveEntry = 'Windows/BlockRealm/Saved/Worlds/BlockRealmWorld.json'
if ($IncludeWorldSave -and -not (Test-Path -LiteralPath (Join-Path $packageRoot 'BlockRealm\Saved\Worlds\BlockRealmWorld.json'))) {
    throw 'IncludeWorldSave requested but the current world save is missing.'
}
if (-not $OutputFile) {
    $OutputFile = Join-Path $projectRoot 'Deliverables\BlockRealm-Windows.zip'
}
if (Test-Path -LiteralPath $OutputFile) {
    throw 'Archive already exists; pass a different -OutputFile.'
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stream = [IO.File]::Open($OutputFile, [IO.FileMode]::CreateNew)
$archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
$count = 0
try {
    foreach ($file in (Get-ChildItem -LiteralPath $packageRoot -Recurse -File)) {
        $relative = [IO.Path]::GetRelativePath($packageRoot, $file.FullName).Replace([IO.Path]::DirectorySeparatorChar, [char]'/')
        $isWorldSave = $relative.Equals('BlockRealm/Saved/Worlds/BlockRealmWorld.json', [StringComparison]::OrdinalIgnoreCase)
        if (($relative.StartsWith('BlockRealm/Saved/', [StringComparison]::OrdinalIgnoreCase) -and
            -not ($IncludeWorldSave -and $isWorldSave)) -or
            $file.Extension -eq '.pdb') {
            continue
        }
        $entryName = 'Windows/' + $relative
        $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $file.FullName, $entryName, [IO.Compression.CompressionLevel]::Optimal)
        $count++
    }
}
finally {
    $archive.Dispose()
    $stream.Dispose()
}

$archive = [IO.Compression.ZipFile]::OpenRead($OutputFile)
try {
    $requiredEntries = @(
        'Windows/BlockRealm.exe',
        'Windows/BlockRealm/Binaries/Win64/BlockRealm.exe',
        'Windows/BlockRealm/Content/Paks/BlockRealm-Windows.pak',
        'Windows/BlockRealm/Content/Paks/BlockRealm-Windows.utoc',
        'Windows/BlockRealm/Content/Paks/BlockRealm-Windows.ucas',
        'Windows/Engine/Extras/Redist/en-us/vc_redist.x64.exe',
        'Windows/Licenses/MiSans-License-Official.pdf',
        'Windows/Licenses/COPYRIGHT.txt',
        'Windows/Licenses/AssetSources.md',
        'Windows/Licenses/NVIDIA-DLSS-LICENSE.txt',
        'Windows/Licenses/NVIDIA-STREAMLINE-MIT-NOTICES.txt',
        'Windows/Licenses/NVIDIA RTX SDK EULA.pdf',
        'Windows/README.md'
        'Windows/Docs/DeliveryReport.md'
    )
    if ($IncludeWorldSave) { $requiredEntries += $worldSaveEntry }
    foreach ($entryName in $requiredEntries) {
        if (-not $archive.GetEntry($entryName)) { throw "Archive is missing $entryName" }
    }
    if ($archive.Entries | Where-Object {
        ($_.FullName -like 'Windows/BlockRealm/Saved/*' -and
            -not ($IncludeWorldSave -and $_.FullName -eq $worldSaveEntry)) -or
        $_.FullName -like '*.pdb'
    }) {
        throw 'Archive contains unexpected runtime data or debug symbols.'
    }
    if (-not ($archive.Entries | Where-Object { $_.FullName -match '/nvngx_dlss\.dll$' })) {
        throw 'Archive is missing the NVIDIA DLSS runtime.'
    }
    $verified = 0
    foreach ($entry in $archive.Entries) {
        $relative = $entry.FullName.Substring('Windows/'.Length).Replace('/', '\')
        $source = Join-Path $packageRoot $relative
        if ($entry.Length -ne (Get-Item -LiteralPath $source).Length) {
            throw "Archive size mismatch: $($entry.FullName)"
        }
        $entryStream = $entry.Open()
        $hasher = [Security.Cryptography.SHA256]::Create()
        try {
            $entryHash = [Convert]::ToHexString($hasher.ComputeHash($entryStream))
        }
        finally { $entryStream.Dispose(); $hasher.Dispose() }
        if ($entryHash -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) {
            throw "Archive content mismatch: $($entry.FullName)"
        }
        $verified++
    }
}
finally { $archive.Dispose() }

$zipHash = (Get-FileHash -LiteralPath $OutputFile -Algorithm SHA256).Hash.ToLowerInvariant()
@{
    success = $true
    verified_files = $verified
    archive = $OutputFile
    bytes = (Get-Item -LiteralPath $OutputFile).Length
    sha256 = $zipHash
    includes_world_save = [bool]$IncludeWorldSave
    source_commit = (git -C $projectRoot rev-parse HEAD)
    created_at = (Get-Date).ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath ($OutputFile + '.verification.json') -Encoding utf8
Write-Output "PASS: $count package files, $verified SHA-256 matches; world save included: $([bool]$IncludeWorldSave); runtime logs, test saves and debug symbols excluded."
Write-Output "SHA-256: $zipHash"
Write-Output $OutputFile
