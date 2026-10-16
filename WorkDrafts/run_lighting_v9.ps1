param(
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$')]
    [string]$OutputName = 'LightingV9',
    [string[]]$SceneNames = @(),
    [string[]]$ExtraArguments = @(),
    [switch]$IncludeExtended
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$packageRoot = Join-Path $projectRoot 'Deliverables\Windows'
$exe = Join-Path $packageRoot 'BlockRealm.exe'
$root = Join-Path (Join-Path $projectRoot 'Deliverables\Validation') $OutputName
$savedRoot = Join-Path $packageRoot 'BlockRealm\Saved'
# BlockWorld::GetSavePath uses these two isolated directories for smoke/lighting flags.
# Keep -BlockRealmSmoke on every probe so older packages also isolate roof runs.
$isolatedSaves = @(
    (Join-Path $savedRoot 'ValidationWorld\BlockRealmWorld.json'),
    (Join-Path $savedRoot 'LightingProbeWorld\BlockRealmWorld.json')
)

function Assert-FreshArtifact {
    param([string]$Path, [datetime]$StartedUtc)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing artifact: $Path"
    }
    $artifact = Get-Item -LiteralPath $Path
    if ($artifact.Length -eq 0 -or $artifact.LastWriteTimeUtc -lt $StartedUtc) {
        throw "Empty or stale artifact: $Path"
    }
}

function Assert-LightingReport {
    param([psobject]$Report, [string]$ExpectedScene, [string[]]$ProbeArguments = @())
    if ($null -eq $Report -or $Report.success -isnot [bool] -or -not $Report.success) {
        throw 'runtime.json must contain success=true.'
    }
    if ($Report.pending_chunks -isnot [ValueType] -or $Report.pending_chunks -is [bool] -or
        [double]$Report.pending_chunks -ne 0) {
        throw 'runtime.json must contain numeric pending_chunks=0.'
    }
    if ($Report.scene -isnot [string] -or [string]::IsNullOrWhiteSpace($Report.scene)) {
        throw 'runtime.json must identify its scene.'
    }
    if ($ExpectedScene -and $Report.scene -ne $ExpectedScene) {
        throw "Wrong runtime.json scene: expected $ExpectedScene, found $($Report.scene)."
    }
    # Rendering diagnostics remain compatible with earlier report fields. The
    # permanently-disabled player-light contract below is mandatory in every scene.
    $numericFields = @(
        'player_z', 'time_of_day', 'sun_lux', 'night_fill_intensity', 'enclosure_weight',
        'view_yaw', 'view_pitch', 'cave_fill_intensity', 'cave_fill_target', 'outdoor_fill_target',
        'cave_fill_cached_intensity', 'outdoor_fill_cached_intensity', 'world_torch_light_count', 'world_glow_light_count',
        'cave_fill_radius', 'hit_lighting_allowed', 'hit_lighting_mode',
        'postprocess_lumen_ray_lighting_mode', 'postprocess_gi_method', 'procedural_mesh_raytracing',
        'lumen_diffuse_indirect_allowed', 'lumen_screen_probe_gather', 'lumen_screen_probe_hardware_raytracing',
        'non_nanite_coarse_pages', 'frame_samples', 'average_frame_ms', 'average_fps', 'p95_frame_ms'
    )
    foreach ($field in $numericFields) {
        $property = $Report.PSObject.Properties[$field]
        if ($null -ne $property -and ($property.Value -isnot [ValueType] -or $property.Value -is [bool] -or
            [double]::IsNaN([double]$property.Value) -or [double]::IsInfinity([double]$property.Value))) {
            throw "runtime.json $field must be a finite number."
        }
    }
    # UE 5.8: ray-lighting overrides SurfaceCache=1, HitLighting=3; GI Lumen=1.
    $numericRequirements = @{
        hit_lighting_allowed = 1; hit_lighting_mode = 1
        postprocess_lumen_ray_lighting_mode = 3; postprocess_gi_method = 1
        procedural_mesh_raytracing = 1
    }
    $booleanRequirements = @{
        raytracing_supported = $true; raygen_supported = $true
        rt_indirect_dispatch_supported = $true; hit_lighting_requirements_met = $true
    }
    $renderPath = $Report.PSObject.Properties['render_path']
    if ($null -ne $renderPath) {
        if ($renderPath.Value -isnot [string] -or [string]::IsNullOrWhiteSpace($renderPath.Value)) {
            throw 'runtime.json render_path must name a supported render path.'
        }
        $numericRequirements.lumen_diffuse_indirect_allowed = 1
        $numericRequirements.lumen_screen_probe_gather = 1
        $numericRequirements.lumen_screen_probe_hardware_raytracing = 1
        $booleanRequirements.render_path_requirements_met = $true
        switch -CaseSensitive ($renderPath.Value) {
            'hwrt_hit_lighting' { }
            'hwrt_surface_cache_compatibility' {
                $numericRequirements.hit_lighting_allowed = 0
                $numericRequirements.postprocess_lumen_ray_lighting_mode = 1
                $booleanRequirements.hit_lighting_requirements_met = $false
                if ($Report.lighting_compatibility_reason -isnot [string] -or
                    [string]::IsNullOrWhiteSpace($Report.lighting_compatibility_reason)) {
                    throw 'Compatibility render path requires a nonempty lighting_compatibility_reason.'
                }
            }
            default { throw "Unknown runtime.json render_path: $($renderPath.Value)" }
        }
    }
    foreach ($requirement in $numericRequirements.GetEnumerator()) {
        $property = $Report.PSObject.Properties[$requirement.Key]
        if ($null -eq $property -and $null -ne $renderPath) {
            throw "runtime.json render_path requires $($requirement.Key)."
        }
        if ($null -ne $property -and $property.Value -ne $requirement.Value) {
            throw "runtime.json $($requirement.Key) must equal $($requirement.Value), found $($property.Value)."
        }
    }
    foreach ($requirement in $booleanRequirements.GetEnumerator()) {
        $property = $Report.PSObject.Properties[$requirement.Key]
        if ($null -eq $property -and $null -ne $renderPath) {
            throw "runtime.json render_path requires $($requirement.Key)."
        }
        if ($null -ne $property -and ($property.Value -isnot [bool] -or $property.Value -ne $requirement.Value)) {
            throw "runtime.json $($requirement.Key) must be $($requirement.Value)."
        }
    }
    $runtimeGateFields = @('raytracing_runtime_enabled', 'raytracing_view_allowed', 'lumen_gi_show_flags_enabled')
    $reportedRuntimeGates = @($runtimeGateFields | Where-Object { $null -ne $Report.PSObject.Properties[$_] })
    if ($reportedRuntimeGates.Count -gt 0) {
        foreach ($field in $runtimeGateFields) {
            $property = $Report.PSObject.Properties[$field]
            if ($null -eq $property -or $property.Value -isnot [bool] -or -not $property.Value) {
                throw "runtime.json must report all runtime/view GI gates as true; invalid or missing $field."
            }
        }
    }
    $failures = $Report.PSObject.Properties['failed']
    if ($null -ne $failures -and @($failures.Value).Count -gt 0) {
        throw "Probe reported failures: $($failures.Value -join '; ')"
    }
    $fixtureReady = $Report.PSObject.Properties['fixture_ready']
    if (($ExpectedScene -like 'roof_*' -or $ExpectedScene -eq 'mouth') -and $null -ne $fixtureReady -and
        ($fixtureReady.Value -isnot [bool] -or -not $fixtureReady.Value)) {
        throw 'Probe fixture did not finish construction.'
    }
    # The character never emits light, including caves and night. Genuine world
    # torches/glowing minerals keep their lights; an unlit cave may remain dark.
    foreach ($field in @('player_fill_disabled', 'player_fill_components_registered', 'world_local_lights_valid')) {
        $property = $Report.PSObject.Properties[$field]
        if ($null -eq $property -or $property.Value -isnot [bool] -or -not $property.Value) {
            throw "runtime.json must report $field=true in every lighting scene."
        }
    }
    foreach ($field in @('cave_fill_visible', 'night_fill_visible')) {
        $property = $Report.PSObject.Properties[$field]
        if ($null -eq $property -or $property.Value -isnot [bool] -or $property.Value) {
            throw "runtime.json must report $field=false in every lighting scene."
        }
    }
    foreach ($field in @('cave_fill_intensity', 'night_fill_intensity', 'cave_fill_target', 'outdoor_fill_target',
        'cave_fill_cached_intensity', 'outdoor_fill_cached_intensity')) {
        $property = $Report.PSObject.Properties[$field]
        if ($null -eq $property -or $property.Value -isnot [ValueType] -or $property.Value -is [bool] -or
            [double]::IsNaN([double]$property.Value) -or [double]::IsInfinity([double]$property.Value) -or
            [Math]::Abs([double]$property.Value) -gt .000001) {
            throw "runtime.json $field must be numeric zero in every lighting scene."
        }
    }
    foreach ($field in @('world_torch_light_count', 'world_glow_light_count')) {
        $property = $Report.PSObject.Properties[$field]
        if ($null -eq $property -or $property.Value -isnot [ValueType] -or $property.Value -is [bool] -or
            [double]::IsNaN([double]$property.Value) -or [double]::IsInfinity([double]$property.Value) -or
            [double]$property.Value -lt 0 -or [double]$property.Value -ne [Math]::Truncate([double]$property.Value)) {
            throw "runtime.json $field must be a nonnegative integer."
        }
    }
    if ($Report.player_fill_mode -ne 'permanently_disabled' -or
        $Report.cave_darkness_policy -isnot [string] -or [string]::IsNullOrWhiteSpace($Report.cave_darkness_policy)) {
        throw 'runtime.json must describe permanent player-light removal and the expected darkness of unlit caves.'
    }
    $fillOverride = $Report.PSObject.Properties['player_fill_disable_override']
    $requestedFillOverride = '-BRDisablePlayerFill' -in $ProbeArguments
    if ($null -eq $fillOverride -or $fillOverride.Value -isnot [bool] -or $fillOverride.Value -ne $requestedFillOverride) {
        throw 'runtime.json player_fill_disable_override must match the legacy comparison argument.'
    }
    foreach ($angle in @('Yaw', 'Pitch')) {
        $field = 'view_' + $angle.ToLowerInvariant()
        $property = $Report.PSObject.Properties[$field]
        if ($null -eq $property) { continue }
        $prefix = "-BRLighting$angle="
        $argument = $ProbeArguments | Where-Object { $_.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) } | Select-Object -First 1
        $expected = if ($angle -eq 'Yaw') {
            if ($ExpectedScene -like 'roof_*' -or $ExpectedScene -eq 'mouth') { 0.0 } else { 35.0 }
        } else { -8.0 }
        if ($argument) {
            $expected = [double]::Parse($argument.Substring($prefix.Length), [Globalization.CultureInfo]::InvariantCulture)
        }
        if ([Math]::Abs([Math]::IEEERemainder([double]$property.Value - $expected, 360.0)) -gt .2) {
            throw "runtime.json $field does not match requested angle $expected."
        }
    }
}

function Assert-LightingLog {
    param([string]$Path)
    # Driver/Streamline warnings alone are not lighting failures.
    $failurePattern = '(?i)\bFatal(?:\s+error)?\s*:|\bAssert(?:ion)?\s+failed\b|\bUnhandled\s*Exception\b|(?:Virtual\s*Shadow\s*Map|\bVSM\b).*(?:overflow|out\s+of.*pages|page\s*pool.*exhaust)|(?:overflow|page\s*pool.*exhaust).*(?:Virtual\s*Shadow\s*Map|\bVSM\b)'
    $failure = Select-String -LiteralPath $Path -Pattern $failurePattern | Select-Object -First 1
    if ($failure) {
        throw "Runtime failure at log line $($failure.LineNumber): $($failure.Line.Trim())"
    }
}

$scenes = @(
    @{ Name = 'CaveLighting';    Arguments = @('-BRCaveLighting');    Scene = 'cave';    Shot = 'UndergroundLighting.png' },
    @{ Name = 'SurfaceLighting'; Arguments = @('-BRSurfaceLighting'); Scene = 'surface'; Shot = 'SurfaceLighting.png' },
    @{ Name = 'NightLighting';   Arguments = @('-BRNightLighting');   Scene = 'night';   Shot = 'NightLighting.png' },
    @{ Name = 'DawnLighting';    Arguments = @('-BRDawnLighting');    Scene = 'dawn';    Shot = 'DawnLighting.png' },
    @{ Name = 'DuskLighting';    Arguments = @('-BRDuskLighting');    Scene = 'dusk';    Shot = 'DuskLighting.png' }
)
if ($IncludeExtended) {
    foreach ($direction in @(@{ Name = 'North'; Yaw = 0 }, @{ Name = 'East'; Yaw = 90 },
        @{ Name = 'South'; Yaw = 180 }, @{ Name = 'West'; Yaw = 270 })) {
        $scenes += @{
            Name = "Cave$($direction.Name)Lighting"
            Arguments = @('-BRCaveLighting', "-BRLightingYaw=$($direction.Yaw)", '-BRLightingPitch=-8')
            Scene = 'cave'
            Shot = 'UndergroundLighting.png'
        }
    }
    foreach ($view in @(@{ Name = 'Up'; Pitch = 55 }, @{ Name = 'Down'; Pitch = -55 })) {
        $scenes += @{
            Name = "Cave$($view.Name)Lighting"
            Arguments = @('-BRCaveLighting', '-BRLightingYaw=35', "-BRLightingPitch=$($view.Pitch)")
            Scene = 'cave'
            Shot = 'UndergroundLighting.png'
        }
    }
    foreach ($material in @('Wood', 'Stone', 'Glass', 'Leaves')) {
        $scenes += @{
            Name = "Roof${material}Lighting"
            Arguments = @("-BRRoofLighting=$material")
            Scene = "roof_$($material.ToLowerInvariant())"
            Shot = 'RoofLighting.png'
        }
    }
    $scenes += @(
        @{ Name = 'MouthLighting'; Arguments = @('-BRMouthLighting'); Scene = 'mouth'; Shot = 'MouthLighting.png' },
        @{ Name = 'RoofWoodBackLighting'; Arguments = @('-BRRoofLighting=Wood', '-BRLightingYaw=180'); Scene = 'roof_wood'; Shot = 'RoofLighting.png' },
        @{ Name = 'RoofWoodNoFillLighting'; Arguments = @('-BRRoofLighting=Wood', '-BRLightingYaw=180', '-BRDisablePlayerFill'); Scene = 'roof_wood'; Shot = 'RoofLighting.png' },
        @{ Name = 'RoofGlassNoFillLighting'; Arguments = @('-BRRoofLighting=Glass', '-BRLightingYaw=180', '-BRDisablePlayerFill'); Scene = 'roof_glass'; Shot = 'RoofLighting.png' },
        @{ Name = 'CaveNoFillLighting'; Arguments = @('-BRCaveLighting', '-BRDisablePlayerFill'); Scene = 'cave'; Shot = 'UndergroundLighting.png' },
        @{ Name = 'SurfaceNoFillLighting'; Arguments = @('-BRSurfaceLighting', '-BRDisablePlayerFill'); Scene = 'surface'; Shot = 'SurfaceLighting.png' }
    )
}
if ($SceneNames.Count -gt 0) {
    $unknown = @($SceneNames | Where-Object { $_ -notin $scenes.Name })
    if ($unknown.Count -gt 0) {
        throw "Unknown or disabled scenes: $($unknown -join ', '). Available: $($scenes.Name -join ', '). Use -IncludeExtended for extended probes."
    }
    $scenes = @($scenes | Where-Object { $_.Name -in $SceneNames })
}
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Packaged executable not found: $exe"
}
New-Item -ItemType Directory -Force -Path $root | Out-Null
foreach ($scene in $scenes) {
    $dir = Join-Path $root $scene.Name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $jsonPath = Join-Path $dir 'runtime.json'
    $logPath = Join-Path $dir 'runtime.log'
    $shotPath = Join-Path $dir $scene.Shot
    # Exact files only: never remove normal Worlds saves or whole directories.
    foreach ($file in @($isolatedSaves) + @($jsonPath, $logPath, $shotPath)) {
        if (Test-Path -LiteralPath $file) {
            if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
                throw "Refusing to remove a non-file artifact: $file"
            }
            Remove-Item -LiteralPath $file -Force
        }
    }
    $launchArguments = @('-BlockRealmSmoke') + $scene.Arguments + @(
        '-windowed', '-ResX=1920', '-ResY=1080', '-ForceRes', '-nosplash', '-unattended',
        "-BRValidationDir=`"$dir`"", "-abslog=`"$logPath`""
    )
    $launchArguments += $ExtraArguments
    $startedUtc = [datetime]::UtcNow
    $process = $null
    try {
        $process = Start-Process -FilePath $exe -ArgumentList $launchArguments -WorkingDirectory $packageRoot -WindowStyle Hidden -PassThru
        if (-not $process.WaitForExit(260000)) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            throw 'Probe timed out after 260 seconds.'
        }
        if ($process.ExitCode -ne 0) {
            throw "Process exited with code $($process.ExitCode)."
        }
        Assert-FreshArtifact -Path $logPath -StartedUtc $startedUtc
        Assert-LightingLog -Path $logPath
        Assert-FreshArtifact -Path $jsonPath -StartedUtc $startedUtc
        $report = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
        Assert-LightingReport -Report $report -ExpectedScene $scene.Scene -ProbeArguments $scene.Arguments
        Assert-FreshArtifact -Path $shotPath -StartedUtc $startedUtc
        $seconds = ([datetime]::UtcNow - $startedUtc).TotalSeconds
        $performance = if ($null -ne $report.PSObject.Properties['p95_frame_ms']) {
            ' avg={0:F2}ms p95={1:F2}ms' -f $report.average_frame_ms, $report.p95_frame_ms
        } else { '' }
        $renderPathLabel = if ($null -ne $report.PSObject.Properties['render_path']) { $report.render_path } else { 'legacy' }
        Write-Output ('PASS {0} scene={1} path={2} chunks=0{3} ({4:F1}s)' -f $scene.Name, $report.scene, $renderPathLabel, $performance, $seconds)
    }
    catch {
        throw "FAIL $($scene.Name): $($_.Exception.Message)"
    }
    finally {
        if ($process) { $process.Dispose() }
    }
}
Write-Output "Lighting validation: $($scenes.Count) passed -> $root"
