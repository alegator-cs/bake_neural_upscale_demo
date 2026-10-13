param([Parameter(Mandatory=$true)][string]$MapPath)
$ErrorActionPreference = "Stop"
if (Test-Path -LiteralPath $MapPath) { throw 'Choose a fresh map path; preserve existing files.' }
$mapDirectory = Split-Path -Parent $MapPath
if ($mapDirectory) { New-Item -ItemType Directory -Force -Path $mapDirectory | Out-Null }


$texture = "gothic_block/blocks17e"

function Add-Brush {
    param(
        [System.Collections.Generic.List[string]]$Lines,
        [string]$Name,
        [int]$XMin,
        [int]$YMin,
        [int]$ZMin,
        [int]$XMax,
        [int]$YMax,
        [int]$ZMax
    )

    $Lines.Add("// brush $Name")
    $Lines.Add("{")
    $Lines.Add("brushDef")
    $Lines.Add("{")
    $Lines.Add("( $XMax $YMax $ZMax ) ( $XMin $YMin $ZMax ) ( $XMin $YMax $ZMax ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) $texture 0 0 0")
    $Lines.Add("( $XMax $YMax $ZMin ) ( $XMin $YMax $ZMax ) ( $XMin $YMax $ZMin ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) $texture 0 0 0")
    $Lines.Add("( $XMax $YMin $ZMax ) ( $XMax $YMax $ZMax ) ( $XMax $YMax $ZMin ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) $texture 0 0 0")
    $Lines.Add("( $XMax $YMin $ZMin ) ( $XMin $YMax $ZMin ) ( $XMin $YMin $ZMin ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) $texture 0 0 0")
    $Lines.Add("( $XMin $YMin $ZMax ) ( $XMin $YMax $ZMin ) ( $XMin $YMax $ZMax ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) $texture 0 0 0")
    $Lines.Add("( $XMax $YMin $ZMax ) ( $XMin $YMin $ZMin ) ( $XMin $YMin $ZMax ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) $texture 0 0 0")
    $Lines.Add("}")
    $Lines.Add("}")
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("// Generated minimal neural-light room test map.")
$lines.Add("// Re-run scripts\generate_neural_test_map.ps1 to rebuild this source.")
$lines.Add("{")
$lines.Add('"classname" "worldspawn"')
$lines.Add('"_ambient" "55"')
$lines.Add('"_minlight" "35"')

$rooms = @(
    @{ Name = "south"; YMin = -1600; YMax = -512 },
    @{ Name = "middle"; YMin = -512; YMax = 512 },
    @{ Name = "north"; YMin = 512; YMax = 1600 }
)

foreach ($room in $rooms) {
    Add-Brush $lines "$($room.Name)_floor" -576 $room.YMin -64 576 $room.YMax 0
    Add-Brush $lines "$($room.Name)_ceiling" -576 $room.YMin 512 576 $room.YMax 576
    Add-Brush $lines "$($room.Name)_west_wall" -576 $room.YMin 0 -512 $room.YMax 512
    Add-Brush $lines "$($room.Name)_east_wall" 512 $room.YMin 0 576 $room.YMax 512
}

Add-Brush $lines "south_outer_wall" -576 -1600 0 576 -1536 512
Add-Brush $lines "north_outer_wall" -576 1536 0 576 1600 512

foreach ($wall in @(@{ Name = "south_middle"; YMin = -544; YMax = -512 }, @{ Name = "middle_north"; YMin = 512; YMax = 544 })) {
    Add-Brush $lines "$($wall.Name)_left_jamb" -576 $wall.YMin 0 -96 $wall.YMax 512
    Add-Brush $lines "$($wall.Name)_right_jamb" 96 $wall.YMin 0 576 $wall.YMax 512
    Add-Brush $lines "$($wall.Name)_lintel" -96 $wall.YMin 224 96 $wall.YMax 512
}

$lines.Add("}")
$lines.Add("{")
$lines.Add('"classname" "info_player_deathmatch"')
$lines.Add('"origin" "0 -320 32"')
$lines.Add('"angle" "90"')
$lines.Add("}")

foreach ($light in @(
    @{ X = -224; Y = -1040; Z = 352; Intensity = 1550; Color = "1.00 0.48 0.30" },
    @{ X =  224; Y = -1040; Z = 352; Intensity = 1300; Color = "1.00 0.36 0.24" },
    @{ X = -224; Y =     0; Z = 352; Intensity = 1650; Color = "0.38 1.00 0.58" },
    @{ X =  224; Y =     0; Z = 352; Intensity = 1450; Color = "0.20 0.82 1.00" },
    @{ X = -224; Y =  1040; Z = 352; Intensity = 1500; Color = "0.30 0.52 1.00" },
    @{ X =  224; Y =  1040; Z = 352; Intensity = 1350; Color = "0.62 0.42 1.00" }
)) {
    $lines.Add("{")
    $lines.Add('"classname" "light"')
    $lines.Add("""origin"" ""$($light.X) $($light.Y) $($light.Z)""")
    $lines.Add("""light"" ""$($light.Intensity)""")
    $lines.Add("""_color"" ""$($light.Color)""")
    $lines.Add("}")
}

Set-Content -LiteralPath $mapPath -Value $lines -Encoding ASCII
Write-Output "Wrote $mapPath"
