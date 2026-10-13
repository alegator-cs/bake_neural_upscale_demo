param(
    [Parameter(Mandatory=$true)][string]$Q3Map2,
    [Parameter(Mandatory=$true)][string]$OpenArenaRoot,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$compiler = (Resolve-Path -LiteralPath $Q3Map2).Path
$gameBase = (Resolve-Path -LiteralPath $OpenArenaRoot).Path
$sourceMap = Join-Path $PSScriptRoot 'neural_test.map'
if (-not (Test-Path -LiteralPath $sourceMap -PathType Leaf)) { throw 'Missing procedural map source.' }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a fresh output directory; existing maps are preserved.' }
$out = New-Item -ItemType Directory -Path $OutputDirectory
$map = Join-Path $out.FullName 'neural_test.map'
Copy-Item -LiteralPath $sourceMap -Destination $map
& $compiler -game quake3 -fs_basepath $gameBase -fs_basegame baseoa $map
if ($LASTEXITCODE -ne 0) { throw 'q3map2 geometry compilation failed.' }
& $compiler -game quake3 -fs_basepath $gameBase -fs_basegame baseoa -vis $map
if ($LASTEXITCODE -ne 0) { throw 'q3map2 visibility compilation failed.' }
& $compiler -game quake3 -fs_basepath $gameBase -fs_basegame baseoa -light -fast $map
if ($LASTEXITCODE -ne 0) { throw 'q3map2 lighting compilation failed.' }
Write-Output (Join-Path $out.FullName 'neural_test.bsp')
