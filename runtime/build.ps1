param(
    [string]$VulkanBin = $(if($env:VULKAN_SDK){Join-Path $env:VULKAN_SDK 'Bin'}else{''}),
    [string]$MSBuildPath = '',
    [string]$PythonPath = 'python',
    [string]$WindowsSDKVersion = '10.0',
    [switch]$ShadersOnly,
    [switch]$Rebuild
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Find-Tool([string]$Name,[string]$Folder) {
    if($Folder){$candidate=Join-Path $Folder $Name;if(Test-Path -LiteralPath $candidate){return (Get-Item -LiteralPath $candidate).FullName}}
    $command=Get-Command $Name -ErrorAction SilentlyContinue
    if($command){return $command.Source}
    throw "Required tool $Name missing. Supply -VulkanBin or put SDK tools on PATH."
}
$glslc=Find-Tool 'glslc.exe' $VulkanBin
$validator=Find-Tool 'spirv-val.exe' $VulkanBin
$glslang=Find-Tool 'glslangValidator.exe' $VulkanBin
$shaders=@('visibility.vert','visibility.frag','resolve_directional.comp','resolve_small32.comp','visibility_firsthit.comp','resolve_small32_linear.comp','upscale_light.comp','upscale_light_fast.comp','upscale_bilinear_fast.comp','visibility_firsthit_gated.comp','upscale_pixel_residual.comp','upscale_pixel_bilinear.comp','upscale_cnn_display.comp','upscale_cnn_conv0.comp','upscale_cnn_conv1.comp','upscale_cnn_head.comp')
foreach($shader in $shaders) {
    & $glslc --target-env=vulkan1.0 -O "-I$PSScriptRoot\surface" "$PSScriptRoot\surface\$shader" -o "$PSScriptRoot\surface\$shader.spv"
    if($LASTEXITCODE-ne 0){throw "Compile failed: $shader"}
    & $validator --target-env vulkan1.0 "$PSScriptRoot\surface\$shader.spv"
    if($LASTEXITCODE-ne 0){throw "SPIR-V failed: $shader"}
}
& $glslang -V --target-env vulkan1.0 "$PSScriptRoot\quake3e\code\renderervk\shaders\neural_mlp_slim.comp" -o "$PSScriptRoot\neural_mlp_slim.spv"
if($LASTEXITCODE-ne 0){throw 'Presentation shader failed'}
& $validator --target-env vulkan1.0 "$PSScriptRoot\neural_mlp_slim.spv"
if($LASTEXITCODE-ne 0){throw 'Presentation validation failed'}
$sourceHashes=@{}
Get-ChildItem -LiteralPath "$PSScriptRoot\surface" -File |Where-Object {$_.Extension -in @('.comp','.glsl','.vert','.frag')}|ForEach-Object {
    $sourceHashes['surface/'+$_.Name]=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
}
$sourceHashes['quake3e/code/renderervk/shaders/neural_mlp_slim.comp']=(Get-FileHash -LiteralPath "$PSScriptRoot\quake3e\code\renderervk\shaders\neural_mlp_slim.comp" -Algorithm SHA256).Hash.ToLowerInvariant()
$spvHashes=@{}
foreach($shader in $shaders){$rel="surface/$shader.spv";$spvHashes[$rel]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $rel) -Algorithm SHA256).Hash.ToLowerInvariant()}
$spvHashes['neural_mlp_slim.spv']=(Get-FileHash -LiteralPath "$PSScriptRoot\neural_mlp_slim.spv" -Algorithm SHA256).Hash.ToLowerInvariant()
@{surface_flags_abi=2;utc=[DateTime]::UtcNow.ToString('o');source_sha256=$sourceHashes;spv_sha256=$spvHashes}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$PSScriptRoot\shader_build_receipt.json" -Encoding utf8
& $PythonPath -B "$PSScriptRoot\embed.py"
if($LASTEXITCODE-ne 0){throw 'Embedding failed'}
if($ShadersOnly){Write-Output '17 shaders compiled and validated; no engine or GPU execution.';return}
if(-not $MSBuildPath) {
    $tool=Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if($tool){$MSBuildPath=$tool.Source}
    else {
        $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if(Test-Path -LiteralPath $vswhere){$installation=& $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath;if($installation){$MSBuildPath=Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'}}
    }
}
if(-not $MSBuildPath -or -not(Test-Path -LiteralPath $MSBuildPath)){throw 'MSBuild missing: install Visual Studio C++ Build Tools or supply -MSBuildPath.'}
$oldSdk=$env:MSBUILDSDKREFERENCEDIRECTORY
try {
    if(-not $oldSdk){$env:MSBUILDSDKREFERENCEDIRECTORY=Join-Path ${env:ProgramFiles(x86)} 'Microsoft SDKs'}
    foreach($project in @('renderervk','quake3e')) {
        $target=if($Rebuild){'Rebuild'}else{'Build'}
        & $MSBuildPath "$PSScriptRoot\quake3e\code\win32\msvc2017\$project.vcxproj" "/t:$target" /p:Configuration=Release /p:Platform=x64 "/p:WindowsTargetPlatformVersion=$WindowsSDKVersion" /p:VCLibPackagePath= /p:VcpkgEnabled=false /p:MultiProcessorCompilation=false /m:1 /nodeReuse:false /nologo /verbosity:minimal *> "$PSScriptRoot\$project-build.log"
        if($LASTEXITCODE-ne 0){Get-Content -LiteralPath "$PSScriptRoot\$project-build.log" -Tail 40;throw "$project build failed"}
        Write-Output "$project build succeeded"
    }
}finally{$env:MSBUILDSDKREFERENCEDIRECTORY=$oldSdk}
New-Item -ItemType Directory -Path "$PSScriptRoot\out" -Force |Out-Null
Copy-Item -LiteralPath "$PSScriptRoot\quake3e\code\win32\msvc2017\output\quake3e.x64.exe" -Destination "$PSScriptRoot\out\quake3e.x64.exe" -Force
Write-Output "Built: $PSScriptRoot\out\quake3e.x64.exe"

