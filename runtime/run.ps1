[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OpenArenaPath,
    [string]$EnginePath = '',
    [string]$AssetsPath = '',
    [string]$HomePath = '',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$MapName = 'neural_test',
    [ValidateSet(12000017, -1)][int]$FrozenViewId = 12000017,
    [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# This script runs only when the user invokes it. PrepareOnly starts no process.
$repo = Split-Path -Parent $PSScriptRoot
if (-not $EnginePath) { $EnginePath = Join-Path $PSScriptRoot 'out\quake3e.x64.exe' }
if (-not $AssetsPath) { $AssetsPath = Join-Path $repo 'assets' }
if (-not $HomePath) { $HomePath = Join-Path $PSScriptRoot 'session' }
$assetRoot = (Get-Item -LiteralPath $AssetsPath).FullName
$oa = (Get-Item -LiteralPath $OpenArenaPath).FullName
$engine = (Get-Item -LiteralPath $EnginePath).FullName
$previewHome = [IO.Path]::GetFullPath($HomePath)
if (-not (Test-Path -LiteralPath (Join-Path $oa 'baseoa\pak0.pk3') -PathType Leaf)) {
    throw 'OpenArenaPath must contain your installed baseoa/pak0.pk3. Game files are not bundled.'
}
if ([String]::Equals($previewHome, $oa, [StringComparison]::OrdinalIgnoreCase) -or
    $previewHome.StartsWith($oa.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'HomePath must be a separate writable directory outside the OpenArena installation.'
}
foreach ($value in @($oa, $engine, $previewHome)) {
    if ($value.Contains('"') -or $value.Contains("`r") -or $value.Contains("`n")) {
        throw 'Paths containing quotes or newlines cannot be represented safely in this engine command line.'
    }
}

$expected = @(
    @('matched_A.nlsf', 788096, '546a3d123111056e155f4f0491245775375f4047711441997072cb98bd944f01'),
    @('matched_A.nlsr', 272908, '02707afc205514bdd4a70675bc5e74e3e08f483783545822ff2b71406f062ece'),
    @('matched_C16.nlr4', 16256, 'd56767cf48ff7e2933867c8a7ddec43a4dabc4808eb5b1d53c5bf6e29441e3a5'),
    @('triangles.bin', 1224832, '657e3d200dcf36412bec2e4b3b3730737a5de07a8e8a49df6e2ba80bb9b1c9ce'),
    @('materials.bin', 128, 'c5ff9715587f4ae1bc0d31c9c3bc98cdaa802a53e1a91e9950f0df468b049f16'),
    @('visibility.bvh', 240368, 'c9fd9986d2fb6ca30b19b31a5a08514a684d615211af23ef0b579f4c4fbf2b18')
)
$transport = @(
    @('maps\neural_transport.bin', 25232, '8f7d4f434400cc6d61b88e7f93ec1d2953fb6db7e83e251cb6a61bb806544c6b'),
    @('textures\neural_transport\reference_480.tga', 518444, '6b4cf9296210235a64c74801949968abe47b7cb6cb2bf00f7c1170e6430d98a5')
)
function Assert-Asset([string]$Path, [long]$Bytes, [string]$Sha) {
    $item = Get-Item -LiteralPath $Path
    $actual = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($item.Length -ne $Bytes -or $actual -ne $Sha) { throw "Frozen October 10 asset changed: $Path" }
}
foreach ($row in $expected) { Assert-Asset (Join-Path $assetRoot $row[0]) $row[1] $row[2] }
foreach ($row in $transport) { Assert-Asset (Join-Path (Join-Path $assetRoot 'transport') $row[0]) $row[1] $row[2] }

$game = Join-Path $previewHome 'baseoa'
$surface = Join-Path $game 'surface'
New-Item -ItemType Directory -Force -Path $surface | Out-Null
foreach ($row in $expected) {
    Copy-Item -LiteralPath (Join-Path $assetRoot $row[0]) -Destination (Join-Path $surface $row[0]) -Force
}
foreach ($row in $transport) {
    $destination = Join-Path $game $row[0]
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path (Join-Path $assetRoot 'transport') $row[0]) -Destination $destination -Force
}

# The source-generated neural_test BSP must be compiled locally, or a caller
# may choose a separately installed map. Only ZIP directory metadata is read.
$mapAvailable = (Test-Path -LiteralPath (Join-Path $game "maps\$MapName.bsp") -PathType Leaf) -or
                (Test-Path -LiteralPath (Join-Path $oa "baseoa\maps\$MapName.bsp") -PathType Leaf)
if (-not $mapAvailable) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    foreach ($pak in Get-ChildItem -LiteralPath (Join-Path $oa 'baseoa') -Filter '*.pk3' -File) {
        $archive = [IO.Compression.ZipFile]::OpenRead($pak.FullName)
        try {
            if ($null -ne $archive.GetEntry("maps/$MapName.bsp")) { $mapAvailable = $true; break }
        } finally { $archive.Dispose() }
    }
}
if (-not $mapAvailable) {
    throw "Map $MapName is missing. Compile reproduction/map/neural_test.map into HomePath/baseoa/maps using your q3map2, or select an installed map with -MapName. No BSP is bundled."
}

$config = @(
    'set r_surfaceBenchmark 0', 'set r_surfacePose 0', 'set r_surfaceMode 2',
    'set r_surfaceVisibility 1', 'set r_surfaceUpscale 10', 'set r_transportLockView 0',
    'set r_surfaceBase surface/matched_A.nlsf', 'set r_surfaceCorrection surface/matched_A.nlsr',
    'set r_surfaceCnnModel surface/matched_C16.nlr4', "set r_surfaceBatchViewID $FrozenViewId",
    'set com_maxfps 60', 'set r_swapInterval 0', 'set cg_fov 35',
    'set cg_drawGun 0', 'set cg_draw2D 0', 'set cl_freelook 1', 'set in_nograb 0',
    'set developer 0', 'set con_notifytime 0', 'set r_neuralTransport 1',
    'set r_neuralModel maps/neural_transport.bin',
    'set r_transportPlate textures/neural_transport/reference_480.tga',
    'set bot_enable 0', 'set com_introplayed 1',
    'bind w +forward', 'bind s +back', 'bind a +moveleft', 'bind d +moveright',
    'bind SPACE +moveup', 'bind CTRL +movedown', 'bind F10 noclip',
    'bind F7 "r_surfaceBatchViewID -1; r_surfacePose 0; r_transportLockView 0; echo FREE CAMERA"',
    "devmap $MapName", 'wait 120', 'noclip',
    'setviewpos 200 -216 70 11.054089', 'wait 3', 'setviewangles -1.647392 11.054089 0'
)
$configFile = Join-Path $game 'c16_release.cfg'
$config | Set-Content -LiteralPath $configFile -Encoding ASCII
$settings = @(
    @('fs_basepath', $oa), @('fs_homepath', $previewHome), @('fs_basegame', 'baseoa'), @('logfile', '2'),
    @('r_fullscreen', '0'), @('r_noborder', '1'), @('r_mode', '-1'),
    @('r_customWidth', '1920'), @('r_customHeight', '1080'), @('s_initsound', '0'),
    @('r_fbo', '1'), @('r_overBrightBits', '0'), @('r_gamma', '1'),
    @('r_neuralFastBuffers', '1'), @('r_neuralFastScale', '1'),
    @('r_surfaceMode', '2'), @('r_surfaceUpscale', '10'),
    @('r_surfaceBase', 'surface/matched_A.nlsf'), @('r_surfaceCorrection', 'surface/matched_A.nlsr'),
    @('r_surfaceCnnModel', 'surface/matched_C16.nlr4'), @('r_neuralView', '6'), @('in_mouse', '-1')
)
if ($settings.Count + 1 -gt 31) { throw 'Engine startup console command limit exceeded.' }
$arguments = foreach ($setting in $settings) {
    '+set'; $setting[0]; '"' + [string]$setting[1] + '"'
}
$arguments += @('+exec', 'c16_release.cfg')
$argumentLine = $arguments -join ' '
if ($argumentLine -match '[^\x00-\x7F]' -or [Text.Encoding]::ASCII.GetByteCount($argumentLine) -gt 1000) {
    throw 'Engine startup command line exceeds its ASCII/1024-byte limit. Use shorter ASCII installation and home paths.'
}
$receipt = [ordered]@{
    schema = 'NL_C16_RELEASE_PREVIEW_PREPARATION_1'; status = 'prepared'; launched = $false
    frozen_view_id = $FrozenViewId; extent = @(1920,1080); surface_mode = 2; upscale_mode = 10
    map = $MapName; OpenArenaPath = $oa; EnginePath = $engine; HomePath = $previewHome
    startup_commands = $settings.Count + 1; argument_bytes = [Text.Encoding]::ASCII.GetByteCount($argumentLine)
    engine_sha256 = (Get-FileHash -LiteralPath $engine -Algorithm SHA256).Hash.ToLowerInvariant()
    config_sha256 = (Get-FileHash -LiteralPath $configFile -Algorithm SHA256).Hash.ToLowerInvariant()
    expected_surface_assets = $expected; expected_transport_assets = $transport
    gpu_build_render_train_jobs_from_preparation = 0
}
$receiptFile = Join-Path $previewHome 'PREPARATION.json'
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptFile -Encoding utf8
if ($PrepareOnly) {
    Write-Output "Prepared C16 preview; no process launched. Receipt: $receiptFile"
    return
}
Write-Host 'Opening October 10 ExperimentalC16 at FHD. F7 enables free camera; mouse/WASD, Space/Ctrl and F10 control movement.'
Write-Host 'Close the window or use the console quit command when finished. This launcher starts no training or bake jobs.'
$process = Start-Process -FilePath $engine -ArgumentList $argumentLine -WorkingDirectory (Split-Path -Parent $engine) -WindowStyle Normal -PassThru
$receipt.launched = $true
$receipt.process_id = $process.Id
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptFile -Encoding utf8
