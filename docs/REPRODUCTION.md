# Reproduce the preserved C16 preview

The package includes the exact selected appearance assets, scene geometry and shader source. Start with inference using the preserved assets; offline reconstruction is a separate workflow.

## Windows build and native preview

Prerequisites: Windows x64, PowerShell 7, Visual Studio 2022 C++ tools/Windows SDK, Vulkan SDK 1.3.290.0 (GLSL/SPIR-V tools), Python 3.10+, a complete separately supplied OpenArena 0.8.8 installation and a working q3map2 distribution with its matching DLLs. The reference build used Python 3.12.8 and Windows SDK 10.0.22000.0; build.ps1 accepts an explicit SDK version. OpenArena must include its stock texture and patch/VM PK3s, not only pak0.pk3. No tools or game data are installed by this package.

### Included files and external acquisition

Everything specific to this selected inference pipeline is Git-tracked:

| Included | Role |
|---|---|
| assets/matched_A.nlsf, assets/matched_A.nlsr, assets/matched_C16.nlr4 | Directional bake, small32 correction and trained C16 weights; no training needed to run them. |
| assets/triangles.bin, assets/materials.bin, assets/visibility.bvh | Frozen scene geometry, material records and visibility acceleration. |
| assets/transport/maps/neural_transport.bin, assets/transport/textures/neural_transport/reference_480.tga | Two pinned engine-initialization support files. |
| runtime/quake3e, runtime/surface, runtime/build.ps1, runtime/embed.py, runtime/run.ps1 | Modified engine/renderer, shader source, shader embedding, build and launch scripts. |
| reproduction/map/neural_test.map, generate_map.ps1, compile_map.ps1 | Procedural map source, source generator and BSP compilation recipe. |
| reproduction/scene and reproduction/models | Procedural source scene, frozen mesh/chart data and numerical model definitions for the separate offline workflow. |

The executable, SPIR-V outputs and BSP are generated locally. The ordinary wall texture `textures/gothic_block/blocks17e` and game VMs come from stock OpenArena PK3s. The following tools and game data are external:

The tested Release/x64 engine links the C runtime statically. Its ordinary DLL imports are Windows components (COMCTL32, WINMM, WSOCK32, KERNEL32, USER32, GDI32, ADVAPI32, ole32 and bcrypt); it has no separate VC++ Redistributable, SDL2 or curl DLL dependency. Vulkan is loaded dynamically as vulkan-1.dll and requires the GPU driver's Vulkan support.

| External dependency | Official acquisition and setup |
|---|---|
| PowerShell 7 | Follow [Microsoft's Windows installation instructions](https://learn.microsoft.com/en-us/powershell/scripting/install/installing-powershell-on-windows); commands below use pwsh. |
| Python 3.10+ | Obtain [Python for Windows](https://www.python.org/downloads/windows/); make python available on PATH or pass -PythonPath to build.ps1. Only the standard library is needed for inference preparation. |
| Visual Studio 2022 C++ toolchain | Use the Community or Build Tools bootstrapper in Microsoft's [Visual Studio 2022 release history](https://learn.microsoft.com/en-us/visualstudio/releases/2022/release-history); install the Desktop development with C++ workload, MSVC v143 x64/x86 tools and a Windows SDK, as in [Microsoft's C++ setup guide](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation). Supply -MSBuildPath if discovery does not find it. |
| Windows SDK | Install through the C++ workload or Microsoft's [SDK download/archive](https://developer.microsoft.com/en-us/windows/downloads/sdk-archive/). The clean check used 10.0.22000.0; -WindowsSDKVersion selects an installed version. |
| Vulkan SDK and GPU driver | Obtain the Windows SDK from [LunarG](https://vulkan.lunarg.com/sdk/home), retaining glslc.exe, spirv-val.exe and glslangValidator.exe in its Bin directory. The clean check used 1.3.290.0; pass -VulkanBin or set VULKAN_SDK. Follow [LunarG's Windows runtime guide](https://vulkan.lunarg.com/doc/view/latest/windows/getting_started.html) for the GPU vendor's Vulkan-capable driver, which supplies the dynamically loaded vulkan-1.dll/ICD. SDK installation alone does not supply GPU support. Engine Vulkan headers are included; no SDK linker import library is needed. |
| OpenArena 0.8.8 | From the [OpenArena project file index](https://sourceforge.net/projects/oarena/files/), download openarena-0.8.8.zip and extract the complete game. OpenArenaPath must point to the directory containing baseoa. |
| q3map2 and its DLLs | From [NetRadiant-custom 20260114](https://github.com/Garux/netradiant-custom/releases/tag/20260114), download netradiant-custom-20260114-windows-x86_64.zip and extract the complete distribution. Pass its q3map2.exe path. The tested compiler was 2.5.17n-git-68ecbed; keep the accompanying DLLs beside it. |

The complete stock OpenArena baseoa directory used in the clean check contained these nine PK3s:

```text
pak0.pk3
pak1-maps.pk3
pak2-players-mature.pk3
pak2-players.pk3
pak4-textures.pk3
pak5-TA.pk3
pak6-misc.pk3
pak6-patch085.pk3
pak6-patch088.pk3
```

The tested q3map2.exe depends on these 14 non-system DLLs from its distribution. Copying only the executable is insufficient:

```text
libassimp-6.dll       libbz2-1.dll
libgcc_s_seh-1.dll    libglib-2.0-0.dll
libiconv-2.dll        libintl-8.dll
libjpeg-8.dll         libminizip-1.dll
libpcre2-8-0.dll      libpng16-16.dll
libstdc++-6.dll       libwinpthread-1.dll
libxml2-16.dll        zlib1.dll
```

Obtain the repository through GitHub's source ZIP or use [Git for Windows](https://git-scm.com/download/win):

```powershell
git clone https://github.com/alegator-cs/bake_neural_upscale_demo.git
cd bake_neural_upscale_demo
```

No private model, VM, map sidecar, training dataset or optimizer checkpoint is needed for this inference build. Blender and PyTorch are only needed for the separate offline reconstruction; that recipe states their versions and commands. The maintained project sources for optional offline tooling are [Blender](https://www.blender.org/download/) and [PyTorch's version-specific installation instructions](https://pytorch.org/get-started/previous-versions/).

From the repository root:

```powershell
python tools/verify_assets.py
pwsh -NoProfile -ExecutionPolicy Bypass -File runtime/build.ps1 -VulkanBin "$env:VULKAN_SDK\Bin"
pwsh -NoProfile -ExecutionPolicy Bypass -File reproduction/map/compile_map.ps1 -OpenArenaRoot "D:\Games\OpenArena" -Q3Map2 "D:\Tools\q3map2.exe" -OutputDirectory "work\map"
New-Item -ItemType Directory -Path "runtime\session\baseoa\maps" -Force
Copy-Item -LiteralPath "work\map\neural_test.bsp" -Destination "runtime\session\baseoa\maps\neural_test.bsp"
pwsh -NoProfile -ExecutionPolicy Bypass -File runtime/run.ps1 -OpenArenaPath "D:\Games\OpenArena" -PrepareOnly
pwsh -NoProfile -ExecutionPolicy Bypass -File runtime/run.ps1 -OpenArenaPath "D:\Games\OpenArena"
```

The build.ps1 arguments `-MSBuildPath`, `-PythonPath` and `-WindowsSDKVersion` override tool discovery and defaults. `-ShadersOnly` compiles and validates GLSL without an engine build. Neither build option launches a renderer.

The launcher defaults to runtime/out/quake3e.x64.exe, runtime/session, FHD and a live camera (batch ID -1, pose 0, transport lock 0). It copies only the eight pinned procedural runtime files into an isolated home, verifies their hashes, and uses C16 mode 10 with world/mirror composition (view 7). It reads game content from the supplied installation. `-EnginePath` and `-HomePath` provide explicit overrides; use an absolute path for HomePath. The canonical map compiler needs a fresh output directory; it preserves existing maps. The map source references an external OpenArena texture name, whose bytes are not redistributed. The legacy neural_test.nlm room sidecar is not required by this selected surface path and was absent in the clean interactive test.

PrepareOnly writes/validates the setup and starts no engine. The final command opens the interactive renderer at the earlier composed-demo starting pose (120, -70, 144; pitch 0.328973, yaw 4.610649). Mouse/WASD control movement; Space/Ctrl move vertically and F10 toggles noclip. F5 selects the raw neural image, F6 restores world composition, F7 leaves any frozen camera and F8 shows the original map. `-FrozenViewId 12000017` selects the admitted fixed camera. Raw view 6 deliberately hides the surrounding map; a frozen batch ID deliberately overrides player-camera motion.

These commands use a process-only execution-policy override. The successful clean build used PowerShell 7; Windows PowerShell 5 failed in the restricted validation host before completing the recipe.

The original Windows engine has startup command/argument limits; short ASCII installation/home paths avoid those limits. No automatic background job is scheduled.

## Offline reconstruction

[The detailed recipe](../reproduction/docs/REPRODUCTION.md) includes the procedural scene/template, geometry exporter, 408 camera records, native physical teacher settings, actual LR capture commands, data registry builder, A directional/small32 fits and C16 fit/export.

Original tools were Blender 3.6.0/build c7fc78b81ecb and PyTorch 2.5.0+cu124. The selected three fits each ended at 10,000 updates. The recipe/protocol files specify seeds, loss, masks, padding, exact linear 4x reduction and display handling.

The dataset, optimizer checkpoints and private job receipts are omitted. The provided adapters passed CPU header/source/CLI checks and were not run through another bake/train campaign. A new fit must actually recapture its own corrected LR input before C16 training.

The [native capture checklist](../reproduction/capture/README.md) provides a source-only packet builder for all 408 exact cameras, a completion checker and the remaining deliberate batch-launch steps. The [identity procedure](IDENTITY.md) provides the original portable binary validators and a review-only row inspector. Fresh A admission, rebuild, actual new-A recapture, C16 fitting/selection review, C16 admission and a second rebuild remain separate steps. The original private owner-receipt graph is not reconstructed by these tools, and portable FIT.json alone does not admit assets. The included selected assets are admitted already. Fresh end-to-end retraining and native admission are unvalidated.

## What was checked in this package

On October 13, a tracked-files-only export of public commit 1fe5c104157f553f154b08c1bad5b845c6c5e65e passed all 17 shader compile/validation steps, engine linking and compilation of the tracked map. No executable, BSP or private runtime assets were borrowed. The resulting engine SHA256 was 57630cf704d5d6f0a7d03e546e714ed2a1133ef1cf6c7828014dc990a1bd55f4 (3,201,536 bytes; not a promise of bit-identical builds on other hosts).

The corrected interactive launcher ran with the eight published assets, nine stock OpenArena PK3s and the newly compiled BSP, without the legacy neural_test.nlm or a private VM. Native pixels showed the map and C16 mirror, two actual renderer receipts showed changed position and orientation, and the user confirmed the result worked. CPU asset/ABI checks and preparation passed. Shader numerical code and appearance assets are retained. No new GPU parity, benchmark, bake or training campaign was performed. The three supplied comparison images remain original October 10 evidence.

A saved Blender file-browser directory was redacted from the release template only; geometry/material/light/world/camera bytes are unchanged. Both original scene identity and release-file SHA are documented. [Licensing notes](LICENSING.md) describe the scoped MIT grant for original work, retained inherited notices and excluded external game content.
